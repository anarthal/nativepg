//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/asio/buffer.hpp>
#include <boost/system/detail/error_code.hpp>
#include <boost/system/system_error.hpp>
#include <boost/variant2/variant.hpp>

#include <cstddef>
#include <span>
#include <string_view>

#include "coroutine.hpp"
#include "nativepg/client_errc.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/protocol/async.hpp"
#include "nativepg/protocol/bind.hpp"
#include "nativepg/protocol/close.hpp"
#include "nativepg/protocol/command_complete.hpp"
#include "nativepg/protocol/connection_state.hpp"
#include "nativepg/protocol/data_row.hpp"
#include "nativepg/protocol/describe.hpp"
#include "nativepg/protocol/detail/connect_fsm.hpp"
#include "nativepg/protocol/detail/exec_fsm.hpp"
#include "nativepg/protocol/empty_query_response.hpp"
#include "nativepg/protocol/execute.hpp"
#include "nativepg/protocol/header.hpp"
#include "nativepg/protocol/messages.hpp"
#include "nativepg/protocol/notice_error.hpp"
#include "nativepg/protocol/parse.hpp"
#include "nativepg/protocol/read_message_fsm.hpp"
#include "nativepg/protocol/read_response_fsm.hpp"
#include "nativepg/protocol/ready_for_query.hpp"
#include "nativepg/protocol/startup.hpp"
#include "nativepg/protocol/startup_fsm.hpp"
#include "nativepg/request.hpp"
#include "nativepg/response_handler.hpp"

using namespace nativepg::protocol;
using boost::system::error_code;
using boost::variant2::get_if;
using boost::variant2::holds_alternative;
using detail::connect_fsm;
using detail::exec_fsm;
using detail::read_response_fsm_impl;
using detail::startup_fsm_impl;
using nativepg::client_errc;

read_message_fsm::result read_message_fsm::resume(std::span<const unsigned char> data)
{
    if (msg_size_ == -1)
    {
        // Header. Ensure we have enough data
        if (data.size() < 5u)
            return result(static_cast<std::size_t>(5u - data.size()));

        // Do the parsing
        auto header_result = parse_header(boost::span<const unsigned char, 5>(data));
        if (header_result.has_error())
            return header_result.error();

        // Record the header fields to signal that we're done with the header
        auto header = *header_result;
        msg_type_ = header.type;
        msg_size_ = header.size;
    }

    // Body. Ensure we have enough data. The header is not discarded
    // until the message is fully parsed for simplicity,
    // and the type byte is not included in the length
    BOOST_ASSERT(msg_size_ != -1);
    const auto expected_size = static_cast<std::size_t>(msg_size_ + 1);
    if (data.size() < expected_size)
        return result(static_cast<std::size_t>(expected_size - data.size()));

    // Do the parsing
    auto msg_result = parse(msg_type_, data.subspan(5, expected_size - 5));
    if (msg_result.has_error())
        return msg_result.error();
    return result(*msg_result, expected_size);
}

static std::span<unsigned char> to_span(boost::asio::mutable_buffer buff)
{
    return {static_cast<unsigned char*>(buff.data()), buff.size()};
}

read_message_stream_fsm::result read_message_stream_fsm::resume(
    connection_state& st,
    boost::system::error_code io_ec,
    std::size_t bytes_read
)
{
    // TODO: we could likely rearrange this code to avoid these temporaries and have less state
    read_message_fsm::result res{error_code()};
    switch (resume_point_)
    {
        NATIVEPG_CORO_INITIAL

        while (true)
        {
            res = fsm_.resume(to_span(st.read_buffer.data()));

            if (res.type() == read_message_fsm::result_type::error)
            {
                // An error is always fatal
                return res.error();
            }
            else if (res.type() == read_message_fsm::result_type::message)
            {
                // We have a message. Yield it and then consume the used bytes
                bytes_to_consume_ = res.bytes_consumed();
                NATIVEPG_YIELD(resume_point_, 1, res.message());
                st.read_buffer.consume(bytes_to_consume_);
                fsm_ = {};
            }
            else
            {
                BOOST_ASSERT(res.type() == read_message_fsm::result_type::needs_more);

                // Prepare the buffer and tell the caller to read
                NATIVEPG_YIELD(resume_point_, 2, to_span(st.read_buffer.prepare(res.hint())));

                // Check for read errors
                if (io_ec)
                    return io_ec;

                // Commit the data and try again
                st.read_buffer.commit(bytes_read);
            }
        }
    }

    BOOST_ASSERT(false);
    return error_code();
}

namespace {

struct startup_visitor
{
    nativepg::diagnostics& diag;

    error_code operator()(const error_response& err) const
    {
        diag.assign(err);
        return client_errc::auth_failed;
    }

    error_code operator()(const authentication_ok&) const { return error_code(); }

    error_code operator()(const authentication_kerberos_v5&) const
    {
        return client_errc::auth_kerberos_v5_unsupported;
    }

    error_code operator()(const authentication_cleartext_password&) const
    {
        return client_errc::auth_cleartext_password_unsupported;
    }

    error_code operator()(const authentication_md5_password&) const
    {
        return client_errc::auth_md5_password_unsupported;
    }

    error_code operator()(const authentication_gss&) const { return client_errc::auth_gss_unsupported; }

    error_code operator()(const authentication_sspi&) const { return client_errc::auth_sspi_unsupported; }

    error_code operator()(const authentication_sasl&) const { return client_errc::auth_sasl_unsupported; }

    template <class T>
    error_code operator()(const T&) const
    {
        return client_errc::unexpected_message;
    }
};

}  // namespace

startup_fsm_impl::result startup_fsm_impl::resume(
    connection_state& st,
    diagnostics& diag,
    const any_backend_message& msg
)
{
    switch (resume_point_)
    {
        NATIVEPG_CORO_INITIAL

        // Compose the startup message
        st.write_buffer.clear();
        if (auto ec = serialize(
                startup_message{
                    .user = params_->username,
                    .database = params_->database.empty() ? std::optional<std::string_view>()
                                                          : std::string_view(params_->database),
                    .params = {},
                },
                st.write_buffer
            ))
        {
            return ec;
        }

        // Write it
        NATIVEPG_YIELD(resume_point_, 1, result_type::write)

        // Read the server's response
        NATIVEPG_YIELD(resume_point_, 2, result_type::read)

        // Act upon the server's message
        // TODO: this will have to change once we implement SASL
        if (auto ec = boost::variant2::visit(startup_visitor{diag}, msg))
        {
            return ec;
        }

        // Backend has approved our login request. Now wait until we receive ReadyForQuery
        while (true)
        {
            // Read a message
            NATIVEPG_YIELD(resume_point_, 3, result_type::read)

            // Act upon it
            if (const auto* key = boost::variant2::get_if<backend_key_data>(&msg))
            {
                st.backend_process_id = key->process_id;
                st.backend_secret_key = key->secret_key;
            }
            else if (boost::variant2::holds_alternative<parameter_status>(msg))
            {
                // TODO: record these somehow
            }
            else if (const auto* err = boost::variant2::get_if<error_response>(&msg))
            {
                diag.assign(*err);
                return error_code(client_errc::auth_failed);
            }
            else if (boost::variant2::holds_alternative<notice_response>(msg))
            {
                // TODO: record these somehow
            }
            else if (boost::variant2::holds_alternative<ready_for_query>(msg))
            {
                return error_code();
            }
            else
            {
                return error_code(client_errc::unexpected_message);
            }
        }
    }

    // We should never reach here
    BOOST_ASSERT(false);
    return error_code();
}

startup_fsm::result startup_fsm::resume(
    connection_state& st,
    diagnostics& diag,
    boost::system::error_code io_error,
    std::size_t bytes_read
)
{
    // TODO: this implementation is improvable, changes in reading messages required
    any_backend_message msg;
    startup_fsm_impl::result startup_res{error_code()};
    read_message_stream_fsm::result read_msg_res{error_code()};

    switch (resume_point_)
    {
        NATIVEPG_CORO_INITIAL

        while (true)
        {
            // Call the FSM
            startup_res = impl_.resume(st, diag, msg);
            if (startup_res.type == startup_fsm_impl::result_type::done)
            {
                // We're finished
                return startup_res.ec;
            }
            else if (startup_res.type == startup_fsm_impl::result_type::write)
            {
                // Just write the message
                NATIVEPG_YIELD(resume_point_, 1, result::write(st.write_buffer))

                // Check for errors
                if (io_error)
                    return io_error;
            }
            else
            {
                BOOST_ASSERT(startup_res.type == startup_fsm_impl::result_type::read);

                // Read a message
                while (true)
                {
                    read_msg_res = st.read_msg_stream_fsm.resume(st, io_error, bytes_read);
                    if (read_msg_res.type() == read_message_stream_fsm::result_type::read)
                    {
                        NATIVEPG_YIELD(resume_point_, 2, result::read(read_msg_res.read_buffer()));
                    }
                    else if (read_msg_res.type() == read_message_stream_fsm::result_type::message)
                    {
                        msg = read_msg_res.message();
                        break;
                    }
                    else
                    {
                        BOOST_ASSERT(read_msg_res.type() == read_message_stream_fsm::result_type::error);
                        return read_msg_res.error();
                    }
                }
            }
        }
    }

    BOOST_ASSERT(false);
    return error_code();
}

enum class read_response_fsm_impl::state_t
{
    resume_msg_first = 0,
    resume_query_first,
    resume_query_needs_ready,
    resume_query_rows,
};

read_response_fsm_impl::result read_response_fsm_impl::handle_error(const error_response& err)
{
    // Call the handler with the error
    call_handler(err);
    ++current_;
    state_ = state_t::resume_msg_first;

    // Skip subsequent messages until a sync is found
    // We should always find one because we check that this is the case
    // before sending the request
    for (; current_ < req_->messages().size(); ++current_)
    {
        switch (req_->messages()[current_])
        {
            case request_message_type::sync: return result(result_type::read);
            case request_message_type::flush: break;
            default: call_handler(message_skipped{}); break;
        }
    }

    BOOST_ASSERT(false);
    return error_code(client_errc::request_ends_without_sync);
}

read_response_fsm_impl::result read_response_fsm_impl::advance()
{
    if (++current_ >= req_->messages().size())
        return error_code();
    else
        return result(result_type::read);
}

read_response_fsm_impl::result read_response_fsm_impl::handle_bind(const any_backend_message& msg)
{
    // bind: either (bind_complete, error_response, bind_skipped)
    BOOST_ASSERT(state_ == state_t::resume_msg_first);
    if (const auto* err = get_if<error_response>(&msg))
    {
        // An error finishes this message and makes the server skip everything until
        // sync
        return handle_error(*err);
    }
    else if (const auto* complete = get_if<bind_complete>(&msg))
    {
        // Finishes the bind phase
        call_handler(*complete);
        return advance();
    }
    else
    {
        return error_code(client_errc::unexpected_message);
    }
}

read_response_fsm_impl::result read_response_fsm_impl::handle_close(const any_backend_message& msg)
{
    // close: either (close_complete, error_response, close_skipped)
    BOOST_ASSERT(state_ == state_t::resume_msg_first);
    if (const auto* err = get_if<error_response>(&msg))
    {
        // An error finishes this message and makes the server skip everything until
        // sync
        return handle_error(*err);
    }
    else if (const auto* complete = get_if<close_complete>(&msg))
    {
        // Finishes the bind phase
        call_handler(*complete);
        return advance();
    }
    else
    {
        return error_code(client_errc::unexpected_message);
    }
}

// TODO: we need to differentiate between describe statement and portal
// only the portal version is supported now
read_response_fsm_impl::result read_response_fsm_impl::handle_describe(const any_backend_message& msg)
{
    // describe (portal)
    //   either: row_description, no_data, error_response, describe_skipped
    // describe (statement) TODO: support this. Either
    //   parameter_description, then either (row_description, no_data)
    //   error_response
    //   describe_skipped
    BOOST_ASSERT(state_ == state_t::resume_msg_first);
    if (const auto* err = get_if<error_response>(&msg))
    {
        // An error finishes this message and makes the server skip everything until
        // sync
        return handle_error(*err);
    }
    else if (const auto* descr = get_if<row_description>(&msg))
    {
        // Finishes the describe phase
        call_handler(*descr);
        return advance();
    }
    else if (holds_alternative<no_data>(msg))
    {
        // We transform no_data into an empty row description, for uniformity.
        // Finishes the describe phase.
        call_handler(row_description{});
        return advance();
    }
    else
    {
        return error_code(client_errc::unexpected_message);
    }
}

read_response_fsm_impl::result read_response_fsm_impl::handle_execute(const any_backend_message& msg)
{
    // execute
    //   either:
    //      any number of data_row, then either (command_complete,
    //      empty_query_response, portal_suspended, error_response) execute_skipped
    BOOST_ASSERT(state_ == state_t::resume_msg_first);
    if (const auto* err = get_if<error_response>(&msg))
    {
        // An error finishes this message and makes the server skip everything
        // until sync
        return handle_error(*err);
    }
    else if (const auto* complete = get_if<command_complete>(&msg))
    {
        // Finishes the execution phase
        call_handler(*complete);
        return advance();
    }
    else if (const auto* eq = get_if<empty_query_response>(&msg))
    {
        // TODO: check that this is the only message
        // Finishes the execution phase
        call_handler(*eq);
        return advance();
    }
    else if (const auto* ps = get_if<portal_suspended>(&msg))
    {
        // Finishes the execution phase
        call_handler(*ps);
        return advance();
    }
    else if (const auto* row = get_if<data_row>(&msg))
    {
        // We got a row. This doesn't change state
        return call_handler(*row);
    }
    else
    {
        return error_code(client_errc::unexpected_message);
    }
}

read_response_fsm_impl::result read_response_fsm_impl::handle_parse(const any_backend_message& msg)
{
    // parse: either (parse_complete, error_response, parse_skipped)
    BOOST_ASSERT(state_ == state_t::resume_msg_first);
    if (const auto* err = get_if<error_response>(&msg))
    {
        // An error finishes this message and makes the server skip everything until
        // sync
        return handle_error(*err);
    }
    else if (const auto* complete = get_if<parse_complete>(&msg))
    {
        // Finishes the parse phase
        call_handler(*complete);
        return advance();
    }
    else
    {
        return error_code(client_errc::unexpected_message);
    }
}

read_response_fsm_impl::result read_response_fsm_impl::handle_sync(const any_backend_message& msg)
{
    // sync always returns ReadyForQuery. Getting an error here is a protocol error,
    // as we don't know whether the connection is healthy or not
    BOOST_ASSERT(state_ == state_t::resume_msg_first);
    if (!holds_alternative<ready_for_query>(msg))
        return error_code(client_errc::unexpected_message);
    return advance();
}

read_response_fsm_impl::result read_response_fsm_impl::handle_query(const any_backend_message& msg)
{
    // either
    //    at least one
    //        optional row_description (we synthesize one of not present)
    //        any number of data_row
    //        finalizer: command_complete, empty_query_response, error_response
    //    ready_for_query
    // or empty_query_response
    // or message_skipped (synthesized by us)
    switch (state_)
    {
        case state_t::resume_msg_first:
        case state_t::resume_query_first:
        {
            if (const auto* err = get_if<error_response>(&msg))
            {
                // An error should always be followed by ReadyForQuery
                state_ = state_t::resume_query_needs_ready;
                return call_handler(*err);
            }
            else if (const auto* desc = get_if<row_description>(&msg))
            {
                // Row descriptions are optional, and can only appear at the beginning of
                // a resultset, but should always precede rows
                state_ = state_t::resume_query_rows;
                return call_handler(*desc);
            }
            else if (const auto* complete = get_if<command_complete>(&msg))
            {
                // Query might return command_complete directly, without NoData.
                // Synthesize a fake row_description to help handlers
                state_ = state_t::resume_query_first;
                call_handler(row_description{});
                return call_handler(*complete);
            }
            else if (const auto* eq = get_if<empty_query_response>(&msg))
            {
                // Only allowed as the first and only message. Signals that there was no query to begin with
                if (state_ != state_t::resume_msg_first)
                    return error_code(client_errc::unexpected_message);
                state_ = state_t::resume_query_needs_ready;
                return call_handler(*eq);
            }
            else if (holds_alternative<ready_for_query>(msg) && state_ == state_t::resume_query_first)
            {
                // Not allowed as the only response to a query
                state_ = state_t::resume_msg_first;
                return advance();
            }
            else
            {
                return error_code(client_errc::unexpected_message);
            }
        }
        case state_t::resume_query_rows:
        {
            if (const auto* err = get_if<error_response>(&msg))
            {
                // An error should always be followed by ReadyForQuery
                state_ = state_t::resume_query_needs_ready;
                return call_handler(*err);
            }
            else if (const auto* row = get_if<data_row>(&msg))
            {
                // We got a row. This doesn't change state
                return call_handler(*row);
            }
            else if (const auto* complete = get_if<command_complete>(&msg))
            {
                // This resultset is done
                state_ = state_t::resume_query_first;
                return call_handler(*complete);
            }
            else
            {
                return error_code(client_errc::unexpected_message);
            }
        }
        case state_t::resume_query_needs_ready:
        {
            // Only a sync is allowed here. Not even an error
            if (holds_alternative<ready_for_query>(msg))
            {
                // We're done with the current message
                state_ = state_t::resume_msg_first;
                return advance();
            }
            else
            {
                return error_code(client_errc::unexpected_message);
            }
        }
        default: BOOST_ASSERT(false); return error_code(client_errc::unexpected_message);
    }
}

read_response_fsm_impl::result read_response_fsm_impl::resume(const any_backend_message& msg)
{
    // Some messages may be found interleaved with the expected message flow
    // TODO: actually do something useful with these
    if (boost::variant2::holds_alternative<notice_response>(msg) ||
        boost::variant2::holds_alternative<notification_response>(msg) ||
        boost::variant2::holds_alternative<parameter_status>(msg))
    {
        return result_type::read;
    }

    // The allowed messages depend on the current type
    while (true)
    {
        switch (req_->messages()[current_])
        {
            case request_message_type::bind: return handle_bind(msg);
            case request_message_type::close: return handle_close(msg);
            case request_message_type::describe: return handle_describe(msg);
            case request_message_type::execute: return handle_execute(msg);
            case request_message_type::flush:
            {
                ++current_;
                continue;  // nothing is expected here
            }
            case request_message_type::parse: return handle_parse(msg);
            case request_message_type::query: return handle_query(msg);
            case request_message_type::sync: return handle_sync(msg);
        }
    }
}

read_response_fsm::result read_response_fsm::resume(
    connection_state& st,
    boost::system::error_code io_error,
    std::size_t bytes_read
)
{
    // Attempt to read a message
    while (true)
    {
        auto read_msg_res = st.read_msg_stream_fsm.resume(st, io_error, bytes_read);
        if (read_msg_res.type() == read_message_stream_fsm::result_type::read)
        {
            return result::read(read_msg_res.read_buffer());
        }
        else if (read_msg_res.type() == read_message_stream_fsm::result_type::message)
        {
            // We've got a message, invoke the FSM
            auto res = impl_.resume(read_msg_res.message());

            // If we're done, exit
            if (res.type == read_response_fsm_impl::result_type::done)
                return res.ec;

            // Otherwise, keep reading
            BOOST_ASSERT(res.type == read_response_fsm_impl::result_type::read);
        }
        else
        {
            BOOST_ASSERT(read_msg_res.type() == read_message_stream_fsm::result_type::error);
            return read_msg_res.error();
        }
    }
}

static error_code check_request(const nativepg::request& req)
{
    // Empty requests are not allowed
    if (req.messages().empty())
        return error_code(client_errc::empty_request);

    // To enable appropriate error recovery, requests need to either
    //   1. End with a sync
    //   2. Be a sequence of query messages, with nothing in front of it
    //   3. End with a sequence of query messages, with a sync in front of it
    // TODO: do we need to support requests ending with flush? is there any use case for this?
    bool query_seen = false;
    for (auto it = req.messages().rbegin(); it != req.messages().rend(); ++it)
    {
        switch (*it)
        {
            case nativepg::request_message_type::query: query_seen = true; continue;
            case nativepg::request_message_type::sync: return error_code();
            default:
                return query_seen ? client_errc::request_mixes_simple_advanced_protocols
                                  : client_errc::request_ends_without_sync;
        }
    }

    // There was nothing but queries
    return error_code();
}

exec_fsm::result exec_fsm::resume(
    connection_state& st,
    boost::system::error_code ec,
    std::size_t bytes_transferred
)
{
    if (state_ == state_t::initial)
    {
        // Check that the request is correctly formed
        const request& req = read_fsm_.get_request();
        auto ec = check_request(req);
        if (ec)
            return ec;

        // Perform the response setup
        auto res = read_fsm_.get_handler().setup(req, 0u);
        if (res.ec)
            return res.ec;
        if (res.offset != req.messages().size())
            return result(client_errc::incompatible_response_length);

        state_ = state_t::writing;

        // Write the request
        return result::write(read_fsm_.get_request().payload());
    }

    if (ec)
        return ec;

    // Don't pass the bytes read to the first FSM invocation
    if (state_ == state_t::writing)
    {
        bytes_transferred = 0u;
        state_ = state_t::reading;
    }
    auto act = read_fsm_.resume(st, ec, bytes_transferred);
    switch (act.type())
    {
        case read_response_fsm::result_type::read: return result::read(act.read_buffer());
        case read_response_fsm::result_type::done: return act.error();
        default: BOOST_ASSERT(false); return error_code();
    }
}

static connect_fsm::result to_connect_result(const startup_fsm::result& r)
{
    switch (r.type())
    {
        case startup_fsm::result_type::read: return connect_fsm::result::read(r.read_buffer());
        case startup_fsm::result_type::write: return connect_fsm::result::write(r.write_data());
        default: BOOST_ASSERT(false); return error_code();
    }
}

connect_fsm::result connect_fsm::resume(
    connection_state& st,
    boost::system::error_code ec,
    std::size_t bytes_transferred
)
{
    startup_fsm::result res{error_code()};

    switch (resume_point_)
    {
        NATIVEPG_CORO_INITIAL

        // Physical connect
        NATIVEPG_YIELD(resume_point_, 1, result::connect())

        // If this failed, try to close. Ignore any errors
        if (ec)
        {
            stored_ec_ = ec;
            NATIVEPG_YIELD(resume_point_, 2, result::close())
            return stored_ec_;
        }

        // Call the startup algorithm
        while (true)
        {
            res = startup_.resume(st, st.shared_diag, ec, bytes_transferred);
            if (res.type() == startup_fsm::result_type::done)
                break;
            else
                NATIVEPG_YIELD(resume_point_, 3, to_connect_result(res))
        }

        // Attempt a close if the startup process failed
        if (res.error())
        {
            stored_ec_ = res.error();
            NATIVEPG_YIELD(resume_point_, 4, result::close())
            return stored_ec_;
        }

        // Success
        return error_code();
    }

    BOOST_ASSERT(false);
    return error_code();
}
