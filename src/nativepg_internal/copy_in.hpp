//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_COPY_IN_HPP
#define NATIVEPG_COPY_IN_HPP

#include <boost/capy/buffers.hpp>
#include <boost/capy/cond.hpp>
#include <boost/capy/ex/async_event.hpp>
#include <boost/capy/io/any_buffer_source.hpp>
#include <boost/capy/io_task.hpp>
#include <boost/capy/when_all.hpp>
#include <boost/capy/write.hpp>
#include <boost/container/small_vector.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <system_error>
#include <utility>

#include "nativepg/client_errc.hpp"
#include "nativepg/co_connection.hpp"
#include "nativepg/exclusivity.hpp"
#include "nativepg/exec_state.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/protocol/command_complete_tag.hpp"
#include "nativepg/request.hpp"
#include "nativepg/responses/any_request_message.hpp"
#include "nativepg/responses/detail/response_utils.hpp"
#include "nativepg/responses/response_handler.hpp"

namespace nativepg::detail {

// TODO: I think we should be able to make write_some_copy_data fail
// if the server rejects our data with an error. This should simplify
// all this code.

// What should the writer do next?
enum class copy_in_action
{
    start_copy,   // Send copy data to the server
    reject_copy,  // Reject the copy by sending CopyFail
    finish,       // Exit
};

// Communicates writer and reader. The reader tells the writer what to do
class copy_in_channel
{
    boost::container::small_vector<copy_in_action, 1u> buff_;
    boost::capy::async_event evt_;

public:
    copy_in_channel() = default;

    void send(copy_in_action act)
    {
        buff_.push_back(act);
        evt_.set();
    }

    boost::capy::io_task<copy_in_action> receive()
    {
        // Wait for a value to be present
        if (auto [ec] = co_await evt_.wait(); ec)
            co_return {ec, copy_in_action::finish};

        BOOST_ASSERT(!buff_.empty());

        // Take the value
        auto res = buff_.front();
        buff_.erase(buff_.begin());
        if (buff_.empty())
            evt_.clear();
        co_return {{}, res};
    }
};

// Response handler used by copy_in. Covers the entire request, so it works with
// any request shape, and tracks what copy_in needs to know: whether the server
// rejected the copy (while the response is still being read), how many times
// the server asked us for data, and the number of rows that were copied.
class copy_in_handler
{
    struct
    {
        std::uint64_t rows_{};
        std::size_t num_copies_{};
        bool in_copy_{};
        bool copy_failed_{};
    } state_;
    copy_in_channel& ch_;

public:
    explicit copy_in_handler(copy_in_channel& ch) noexcept : ch_(ch) {}

    handler_setup_result setup(const request& req, std::size_t)
    {
        // The request should contain at least one Query/Execute
        const bool may_copy = std::ranges::any_of(req.messages(), [](request_message_type type) {
            return type == request_message_type::execute || type == request_message_type::query;
        });
        if (!may_copy)
            return make_error_code(client_errc::incompatible_response_type);

        state_ = {};
        return req.messages().size();
    }

    void on_message(const any_request_message& msg, std::size_t, extended_error& err)
    {
        switch (msg.type())
        {
            case any_request_message::kind::copy_in_response:
                ch_.send(state_.num_copies_ == 0u ? copy_in_action::start_copy : copy_in_action::reject_copy);
                ++state_.num_copies_;
                state_.in_copy_ = true;
                break;
            case any_request_message::kind::command_complete:
                // The tag that closes a copy carries the number of copied rows
                // Parsing it is best-effort, as the protocol doesn't make guarantees about its format
                if (state_.in_copy_)
                {
                    std::optional<std::uint64_t> opt_rows;
                    static_cast<void>(
                        protocol::parse_command_complete_tag(msg.get_command_complete().tag, opt_rows)
                    );
                    state_.rows_ = opt_rows.value_or(0u);
                    state_.in_copy_ = false;
                }
                break;
            case any_request_message::kind::error_response:
                detail::store_error(msg.get_error_response(), err);

                // Errors raised by other statements of the request (possible when
                // pipelining) don't require us to stop sending data
                if (state_.in_copy_)
                {
                    state_.copy_failed_ = true;
                    state_.in_copy_ = false;
                }
                break;
            default: break;
        }
    }

    // Did the server reject the copy? May be called while the response is being read
    bool copy_failed() const { return state_.copy_failed_; }

    // How many times did the server ask us for data?
    std::size_t num_copies() const { return state_.num_copies_; }

    // Rows reported by the CommandComplete that closed the copy
    std::uint64_t rows() const { return state_.rows_; }
};

// Adapts write_some_copy_data into capy::WriteStream
struct copy_in_stream_adaptor
{
    co_connection* conn_ptr;
    exec_state* st_ptr;

    template <boost::capy::ConstBufferSequence Buffers>
    boost::capy::io_task<std::size_t> write_some(Buffers&& buffs)
    {
        return conn_ptr->write_some_copy_data(*st_ptr, std::forward<Buffers>(buffs));
    }
};

// Runs a single Copy-in operation until termination.
// Only transport failures are reported: anything else is recorded in copy_ec.
inline boost::capy::io_task<> run_copy(
    co_connection& conn,
    exec_state& st,
    boost::capy::any_buffer_source& source,
    const copy_in_handler& handler,
    std::error_code& copy_ec
)
{
    // write_some_copy_data can write 12 buffers in one go
    std::array<boost::capy::const_buffer, 12u> pull_storage;
    copy_in_stream_adaptor adaptor{&conn, &st};

    while (true)
    {
        // If the server rejected our data, stop.
        // This is the only safe place to stop, because we don't want to
        // leave open frames behind.
        if (handler.copy_failed())
            co_return co_await conn.write_copy_fail(st, "aborted by the client after a server error");

        // Get more data
        auto [pull_ec, buffs] = co_await source.pull(pull_storage);
        if (pull_ec == boost::capy::cond::eof)
            co_return co_await conn.write_copy_done(st);
        if (pull_ec)
        {
            copy_ec = pull_ec;
            co_return co_await conn.write_copy_fail(st, pull_ec.message());
        }

        // Write it. Writing it completely guarantees that we
        // leave the connection in a clean state upon success (no partial frames)
        auto [ec, bytes_written] = co_await boost::capy::write(adaptor, buffs);
        if (ec)
            co_return {ec};  // Errors are fatal

        // We've fully written the bytes
        source.consume(bytes_written);
    }
}

// Writer side of copy_in
// Only transport failures are reported: anything else is recorded in copy_ec.
inline boost::capy::io_task<> copy_in_writer(
    co_connection& conn,
    exec_state& st,
    boost::capy::any_buffer_source& source,
    const copy_in_handler& handler,
    copy_in_channel& channel,
    std::error_code& copy_ec
)
{
    // Send the request
    if (auto [ec] = co_await conn.write_request(st); ec)
        co_return {ec};

    while (true)
    {
        // Wait for the reader to tell us what to do next
        auto [channel_ec, act] = co_await channel.receive();
        if (channel_ec)
            co_return {channel_ec};

        switch (act)
        {
            case copy_in_action::start_copy:
                // Send data to the server
                if (auto [ec] = co_await run_copy(conn, st, source, handler, copy_ec); ec)
                    co_return {ec};
                break;
            case copy_in_action::reject_copy:
                // We hit Copy-in more than once. Reject the copy
                if (auto [ec] = co_await conn.write_copy_fail(st, "unexpected COPY ... FROM STDIN"); ec)
                    co_return {ec};
                break;
            case copy_in_action::finish: co_return {};
            default: BOOST_ASSERT(false); co_return {};
        }
    }
}

// Reader side of copy_in
inline boost::capy::io_task<> copy_in_reader(co_connection& conn, exec_state& st, copy_in_channel& channel)
{
    // This will invoke the handler, which will tell the writer what to do
    while (!st.read_done())
    {
        if (auto [ec] = co_await conn.read_some_response(st); ec)
            co_return {ec};  // Will cancel the writer
    }

    channel.send(copy_in_action::finish);

    co_return {};
}

inline boost::capy::io_task<std::uint64_t> copy_in(
    co_connection& conn,
    const request& req,
    boost::capy::any_buffer_source source,
    diagnostics* diag
)
{
    // Clear diagnostics
    if (diag)
        *diag = diagnostics();

    // Setup. Copy operations can't be multiplexed with other requests
    copy_in_channel channel;
    copy_in_handler handler{channel};
    exec_state st;
    if (auto ec = conn.prepare_request(st, req, &handler, exclusivity::exclusive))
        co_return {ec, 0u};

    // Run the reader and the writer in parallel. The writer only reports transport
    // failures, so that a rejected copy doesn't cancel the reader: draining the
    // response is what keeps the connection usable
    std::error_code copy_ec;
    auto [final_ec, writer_dummy, reader_dummy] = co_await boost::capy::when_all(
        copy_in_writer(conn, st, source, handler, channel, copy_ec),
        copy_in_reader(conn, st, channel)
    );

    // An I/O error happened
    if (final_ec)
        co_return {final_ec, 0u};

    // Did we have more than one Copy-in?
    // This causes a handler error (by sending CopyFail),
    // but we should report this other error, instead
    if (handler.num_copies() > 1u)
        co_return {client_errc::incompatible_response_type, 0u};

    // Did the source fail? This also causes CopyFail
    if (copy_ec)
        co_return {copy_ec, 0u};

    // Did the server report any error?
    // This includes where the server errors the Copy-in request,
    // and never enters Copy-in mode
    if (st.handler_error().code)
    {
        if (diag)
            *diag = st.handler_error().diag;
        co_return {st.handler_error().code, 0u};
    }

    // The request didn't trigger Copy-in
    if (handler.num_copies() == 0u)
        co_return {client_errc::incompatible_response_type, 0u};

    co_return {std::error_code(), handler.rows()};
}

}  // namespace nativepg::detail

#endif
