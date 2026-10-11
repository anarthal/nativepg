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

#include <array>
#include <cstddef>
#include <cstdint>
#include <system_error>
#include <utility>

#include "nativepg/client_errc.hpp"
#include "nativepg/co_connection.hpp"
#include "nativepg/exclusivity.hpp"
#include "nativepg/exec_state.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/request.hpp"
#include "nativepg/responses/any_request_message.hpp"
#include "nativepg/responses/command_info.hpp"
#include "nativepg/responses/detail/response_utils.hpp"
#include "nativepg/responses/response_handler.hpp"
#include "nativepg/write_status.hpp"

namespace nativepg::detail {

// Response handler used by copy_in. Covers the entire request, so it works with
// any request shape, and tracks what copy_in needs to know: whether the server
// rejected the copy (while the response is still being read), how many times
// the server asked us for data, and the number of rows that were copied.
class copy_in_handler
{
    extended_error err_{};
    command_info info_{};
    std::uint64_t rows_{};
    std::size_t num_copies_{};
    bool in_copy_{};
    bool copy_failed_{};

public:
    copy_in_handler() = default;

    handler_setup_result setup(const request& req, std::size_t)
    {
        // TODO: if there isn't at least one Execute or Query, this should fail
        // TODO: *this = X seems sus
        *this = copy_in_handler();
        return req.messages().size();
    }

    void on_message(const any_request_message& msg, std::size_t, extended_error& err)
    {
        switch (msg.type())
        {
            case any_request_message::kind::copy_in_response:
                ++num_copies_;
                in_copy_ = true;
                break;
            case any_request_message::kind::command_complete:
                // The tag that closes a copy carries the number of copied rows
                if (in_copy_)
                {
                    // TODO: can be simplified
                    detail::from_command_complete(info_, msg.get_command_complete());
                    rows_ = info_.affected_rows.value_or(0u);
                    in_copy_ = false;
                }
                break;
            case any_request_message::kind::error_response:
                detail::store_error(msg.get_error_response(), err);

                // Keep our own copy: exec_state::handler_error() may only be
                // accessed once the entire response has been read, and the writer
                // needs to know about errors as soon as they arrive
                if (!err_.code)
                    err_ = err;

                // Errors raised by other statements of the request (possible when
                // pipelining) don't require us to stop sending data
                if (in_copy_)
                {
                    copy_failed_ = true;
                    in_copy_ = false;
                }
                break;
            default: break;
        }
    }

    // Did the server reject the copy? May be called while the response is being read
    bool copy_failed() const { return copy_failed_; }

    // How many times did the server ask us for data?
    std::size_t num_copies() const { return num_copies_; }

    // Rows reported by the CommandComplete that closed the copy
    std::uint64_t rows() const { return rows_; }

    // The first error that the server reported, if any
    const extended_error& error() const { return err_; }
};

// Adapts write_some_copy_data into capy::WriteStream
struct stream_adaptor
{
    co_connection* conn_ptr;
    exec_state* st_ptr;

    template <boost::capy::ConstBufferSequence Buffers>
    boost::capy::io_task<std::size_t> write_some(Buffers&& buffs)
    {
        return conn_ptr->write_some_copy_data(*st_ptr, std::forward<Buffers>(buffs));
    }
};

// Feeds the server with data from source until the source is exhausted, the server
// rejects the copy, or the source fails. Terminates the copy in all cases.
// Only transport failures are reported: anything else is recorded in copy_ec.
inline boost::capy::io_task<> run_copy(
    co_connection& conn,
    exec_state& st,
    boost::capy::any_buffer_source& source,
    const copy_in_handler& handler,
    std::error_code& copy_ec
)
{
    std::array<boost::capy::const_buffer, 16u> pull_storage;
    stream_adaptor adaptor{&conn, &st};

    while (true)
    {
        // If the server rejected our data, stop as soon as we notice it.
        // We can't stop any earlier: whatever we handed to write_some_copy_data
        // has already been framed, and must be transferred before terminating
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
inline boost::capy::io_task<> copy_in_writer(
    co_connection& conn,
    exec_state& st,
    boost::capy::any_buffer_source& source,
    const copy_in_handler& handler,
    boost::capy::async_event& reader_progressed,
    std::error_code& copy_ec
)
{
    // Send the request
    if (auto [ec] = co_await conn.write_request(st); ec)
        co_return {ec};

    std::size_t num_copies = 0u;

    while (true)
    {
        switch (st.write_phase())
        {
            case write_status::done: co_return {};
            case write_status::waiting_for_reader:
                // Only the reader can tell whether the server wants data from us.
                // The event is sticky, so clear it before waiting. There is no
                // suspension point between the check above and the clear,
                // so no notification can be missed
                reader_progressed.clear();
                if (auto [ec] = co_await reader_progressed.wait(); ec)
                    co_return {ec};
                continue;
            default: break;
        }

        // The server is asking us for data
        if (++num_copies > 1u)
        {
            // We only know how to feed a single copy. Terminate this one anyway,
            // so the connection is left in sync, and report the problem
            if (!copy_ec)
                copy_ec = client_errc::invalid_copy_in_request;
            if (auto [ec] = co_await conn.write_copy_fail(st, "unexpected COPY ... FROM STDIN"); ec)
                co_return {ec};
        }
        else if (auto [ec] = co_await run_copy(conn, st, source, handler, copy_ec); ec)
        {
            co_return {ec};
        }
    }
}

// Reader side of copy_in
inline boost::capy::io_task<> copy_in_reader(
    co_connection& conn,
    exec_state& st,
    boost::capy::async_event& reader_progressed
)
{
    while (!st.read_done())
    {
        auto [ec] = co_await conn.read_some_response(st);

        // Let the writer re-evaluate what it should do next
        reader_progressed.set();

        if (ec)
            co_return {ec};
    }

    co_return {};
}

inline boost::capy::io_task<std::uint64_t> copy_in(
    co_connection& conn,
    const request& req,
    boost::capy::any_buffer_source source,
    diagnostics* diag
)
{
    // Setup. Copy operations can't be multiplexed with other requests
    copy_in_handler handler;
    exec_state st;
    if (auto ec = conn.prepare_request(st, req, &handler, exclusivity::exclusive))
        co_return {ec, 0u};

    // Run the reader and the writer in parallel. The writer only reports transport
    // failures, so that a rejected copy doesn't cancel the reader: draining the
    // response is what keeps the connection usable
    boost::capy::async_event reader_progressed;
    std::error_code copy_ec;
    auto [final_ec, writer_dummy, reader_dummy] = co_await boost::capy::when_all(
        copy_in_writer(conn, st, source, handler, reader_progressed, copy_ec),
        copy_in_reader(conn, st, reader_progressed)
    );

    // The connection is left in an unspecified state if this happens
    if (final_ec)
        co_return {final_ec, 0u};

    if (diag)
        *diag = copy_ec ? diagnostics() : handler.error().diag;

    // The source failed, or the request is not one we can drive. The error that our
    // CopyFail triggered is an echo of this one, so don't report it instead
    if (copy_ec)
        co_return {copy_ec, 0u};

    // The server rejected the request or the data we sent
    if (handler.error().code)
        co_return {handler.error().code, 0u};

    // The request never asked us for data
    if (handler.num_copies() != 1u)
        co_return {client_errc::invalid_copy_in_request, 0u};

    co_return {std::error_code(), handler.rows()};
}

}  // namespace nativepg::detail

#endif
