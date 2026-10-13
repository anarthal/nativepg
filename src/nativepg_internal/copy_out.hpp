//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_COPY_OUT_HPP
#define NATIVEPG_COPY_OUT_HPP

#include <boost/capy/buffers.hpp>
#include <boost/capy/buffers/buffer_copy.hpp>
#include <boost/capy/buffers/make_buffer.hpp>
#include <boost/capy/io/any_write_sink.hpp>
#include <boost/capy/io_task.hpp>
#include <boost/capy/when_all.hpp>
#include <boost/container/small_vector.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <system_error>
#include <vector>

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

// How many CopyData frames a single read batch is expected to hold
// before the collection vector needs to allocate
inline constexpr std::size_t copy_out_inline_buffers = 16u;

// Response handler used by copy_out. Covers the entire request, so it works with
// any request shape, and tracks what copy_out needs to know: the copy data produced
// by the current read batch, how many times the server started a copy, and the
// number of rows that were copied.
class copy_out_handler
{
    struct
    {
        std::uint64_t rows_{};
        std::size_t num_copies_{};
        bool in_copy_{};
        bool collecting_{};
    } state_;

    // The payloads seen so far. These are views into the connection's read buffer:
    // see copy_out_reader for the lifetime rules
    boost::container::small_vector<boost::capy::const_buffer, copy_out_inline_buffers> buffs_;

public:
    copy_out_handler() = default;

    handler_setup_result setup(const request& req, std::size_t)
    {
        // The request should contain at least one Query/Execute
        const bool may_copy = std::ranges::any_of(req.messages(), [](request_message_type type) {
            return type == request_message_type::execute || type == request_message_type::query;
        });
        if (!may_copy)
            return make_error_code(client_errc::incompatible_response_type);

        state_ = {};
        buffs_.clear();
        return req.messages().size();
    }

    void on_message(const any_request_message& msg, std::size_t, extended_error& err)
    {
        switch (msg.type())
        {
            case any_request_message::kind::copy_out_response:
                // A request that starts more than one copy is a user error.
                // We keep draining the response, so the connection stays usable,
                // but stop handing data to the sink
                state_.collecting_ = ++state_.num_copies_ == 1u;
                state_.in_copy_ = true;
                break;
            case any_request_message::kind::copy_data:
                if (state_.collecting_)
                {
                    auto data = msg.get_copy_data().data;
                    buffs_.emplace_back(data.data(), data.size());
                }
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

                // The operation is going to fail, so there is no point
                // in handing further data to the sink
                state_.in_copy_ = false;
                state_.collecting_ = false;
                break;
            default: break;
        }
    }

    // The copy data collected since the last call to clear_buffers()
    std::span<const boost::capy::const_buffer> buffers() const { return {buffs_.data(), buffs_.size()}; }

    // Releases the collected views. Keeps the capacity, so the steady state doesn't allocate
    void clear_buffers() { buffs_.clear(); }

    // How many times did the server start a copy?
    std::size_t num_copies() const { return state_.num_copies_; }

    // Rows reported by the CommandComplete that closed the copy
    std::uint64_t rows() const { return state_.rows_; }
};

// Reader side of copy_out. Reads the response, handing the copy data produced
// by each batch to the sink.
// The views collected by the handler point into the connection's read buffer.
// They stay valid for as long as our exec_state holds the reader turn, because
// nothing else can then read (and thus reallocate the buffer). This holds for every
// batch except the one that completes the response: read_some_response releases
// the turn before returning, so that last batch is copied into tail before we
// suspend, and copy_out delivers it once it knows the transfer succeeded.
inline boost::capy::io_task<> copy_out_reader(
    co_connection& conn,
    exec_state& st,
    copy_out_handler& handler,
    boost::capy::any_write_sink& sink,
    std::vector<unsigned char>& tail
)
{
    while (true)
    {
        // Read a batch of messages. This invokes the handler, which collects
        // whatever copy data the batch contained
        if (auto [ec] = co_await conn.read_some_response(st); ec)
            co_return {ec};

        // We've read the response in full, and no longer hold the reader turn
        if (st.read_done())
        {
            auto buffs = handler.buffers();
            tail.resize(boost::capy::buffer_size(buffs));
            boost::capy::buffer_copy(boost::capy::make_buffer(tail), buffs);
            handler.clear_buffers();
            co_return {};
        }

        // Hand the batch to the sink. Writing it completely before reading again
        // is what keeps the views valid. An empty sequence completes immediately,
        // so batches carrying no copy data need no special casing
        auto [ec, bytes] = co_await sink.write(handler.buffers());
        static_cast<void>(bytes);  // write() transfers everything or fails
        handler.clear_buffers();

        // The protocol offers no way to tell the server to stop sending data,
        // so we just stop reading. Destroying the exec_state hands the leftovers
        // to the multiplexer, which keeps the connection usable
        if (ec)
            co_return {ec};
    }
}

inline boost::capy::io_task<std::uint64_t> copy_out(
    co_connection& conn,
    const request& req,
    boost::capy::any_write_sink sink,
    diagnostics* diag
)
{
    // Clear diagnostics
    if (diag)
        *diag = diagnostics();

    // Setup. Unlike Copy-in, Copy-out requires no messages from the client,
    // so it doesn't need to lock the connection
    copy_out_handler handler;
    exec_state st;
    if (auto ec = conn.prepare_request(st, req, &handler, exclusivity::shared))
        co_return {ec, 0u};

    // Run the reader and the writer in parallel, as exec() does. Running them in
    // sequence may deadlock: writing a request bigger than the send buffer can't
    // complete until the server drains it, and the server won't drain it while
    // it's blocked sending copy data that nobody is reading
    std::vector<unsigned char> tail;
    auto [final_ec, writer_dummy, reader_dummy] = co_await boost::capy::when_all(
        conn.write_request(st),
        copy_out_reader(conn, st, handler, sink, tail)
    );

    // The transport or the sink failed. In the latter case the connection is still
    // usable: whatever the server still owes us is discarded by subsequent requests
    if (final_ec)
        co_return {final_ec, 0u};

    // We've read the response in full, so handler_error() is now legal

    // Did the request start more than one copy?
    // We only ever fed the sink from the first one
    if (handler.num_copies() > 1u)
        co_return {client_errc::incompatible_response_type, 0u};

    // Did the server report any error?
    // This includes the case where the server errors the request
    // and never enters Copy-out mode
    if (st.handler_error().code)
    {
        if (diag)
            *diag = st.handler_error().diag;
        co_return {st.handler_error().code, 0u};
    }

    // The request didn't trigger Copy-out
    if (handler.num_copies() == 0u)
        co_return {client_errc::incompatible_response_type, 0u};

    // Deliver the last batch and tell the sink that there is no more data.
    // Only successful transfers get finalized
    const boost::capy::const_buffer tail_buff{tail.data(), tail.size()};
    if (auto [ec, bytes] = co_await sink.write(tail_buff); ec)
        co_return {ec, 0u};
    if (auto [ec] = co_await sink.write_eof(); ec)
        co_return {ec, 0u};

    co_return {std::error_code(), handler.rows()};
}

}  // namespace nativepg::detail

#endif
