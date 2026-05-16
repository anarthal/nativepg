//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/system/error_code.hpp>

#include <cstddef>

#include "nativepg/client_errc.hpp"
#include "nativepg/protocol/connection_state.hpp"
#include "nativepg/protocol/describe.hpp"
#include "nativepg/protocol/messages.hpp"
#include "nativepg/protocol/read_message_fsm.hpp"
#include "nativepg/protocol/read_response_fsm.hpp"
#include "nativepg/request.hpp"
#include "nativepg/response_handler.hpp"

using namespace nativepg::protocol;
using boost::system::error_code;
using detail::read_response_fsm_impl;
using nativepg::client_errc;
using kind = any_backend_message::kind;

enum class read_response_fsm_impl::state_t
{
    msg_first = 0,
    query_first,
    query_needs_ready,
    query_rows,
};

read_response_fsm_impl::result read_response_fsm_impl::handle_error(const error_response& err)
{
    // Call the handler with the error
    call_handler(err);
    ++current_;
    state_ = state_t::msg_first;

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
    // bind: either (bind_complete, error_response)
    BOOST_ASSERT(state_ == state_t::msg_first);
    switch (msg.get_kind())
    {
        case kind::error_response:
            // An error finishes this message and makes the server skip everything until sync
            return handle_error(msg.get_error_response());
        case kind::bind_complete:
            // Finishes the bind phase
            call_handler(msg.get_bind_complete());
            return advance();
        default: return error_code(client_errc::unexpected_message);
    }
}

read_response_fsm_impl::result read_response_fsm_impl::handle_close(const any_backend_message& msg)
{
    // close: either (close_complete, error_response)
    BOOST_ASSERT(state_ == state_t::msg_first);
    switch (msg.get_kind())
    {
        case kind::error_response:
            // An error finishes this message and makes the server skip everything until sync
            return handle_error(msg.get_error_response());
        case kind::close_complete:
            // Finishes the close phase
            call_handler(msg.get_close_complete());
            return advance();
        default: return error_code(client_errc::unexpected_message);
    }
}

// TODO: we need to differentiate between describe statement and portal
// only the portal version is supported now
read_response_fsm_impl::result read_response_fsm_impl::handle_describe(const any_backend_message& msg)
{
    // describe (portal)
    //   either: row_description, no_data, error_response
    // describe (statement) TODO: support this. Either
    //   parameter_description, then either (row_description, no_data)
    //   error_response
    BOOST_ASSERT(state_ == state_t::msg_first);
    switch (msg.get_kind())
    {
        case kind::error_response:
            // An error finishes this message and makes the server skip everything until sync
            return handle_error(msg.get_error_response());
        case kind::row_description:
            // Finishes the describe phase
            call_handler(msg.get_row_description());
            return advance();
        case kind::no_data:
            // We transform no_data into an empty row description, for uniformity.
            // Finishes the describe phase.
            call_handler(row_description{});
            return advance();
        default: return error_code(client_errc::unexpected_message);
    }
}

read_response_fsm_impl::result read_response_fsm_impl::handle_execute(const any_backend_message& msg)
{
    // execute: either:
    //   any number of data_row, then either (command_complete, portal_suspended, error_response)
    //   empty_query_response
    BOOST_ASSERT(state_ == state_t::msg_first);
    switch (msg.get_kind())
    {
        case kind::error_response:
            // An error finishes this message and makes the server skip everything until sync
            return handle_error(msg.get_error_response());
        case kind::command_complete:
            // Finishes the execution phase
            call_handler(msg.get_command_complete());
            return advance();
        case kind::empty_query_response:
            // TODO: check that this is the only message
            // Finishes the execution phase
            call_handler(msg.get_empty_query_response());
            return advance();
        case kind::portal_suspended:
            // Finishes the execution phase
            call_handler(msg.get_portal_suspended());
            return advance();
        case kind::data_row:
            // We got a row. This doesn't change state
            call_handler(msg.get_data_row());
            return result_type::read;
        default: return error_code(client_errc::unexpected_message);
    }
}

read_response_fsm_impl::result read_response_fsm_impl::handle_parse(const any_backend_message& msg)
{
    // parse: either (parse_complete, error_response)
    BOOST_ASSERT(state_ == state_t::msg_first);
    switch (msg.get_kind())
    {
        case kind::error_response:
            // An error finishes this message and makes the server skip everything until sync
            return handle_error(msg.get_error_response());
        case kind::parse_complete:
            // Finishes the parse phase
            call_handler(msg.get_parse_complete());
            return advance();
        default: return error_code(client_errc::unexpected_message);
    }
}

read_response_fsm_impl::result read_response_fsm_impl::handle_sync(const any_backend_message& msg)
{
    // sync always returns ReadyForQuery. Getting an error here is a protocol error,
    // as we don't know whether the connection is healthy or not
    BOOST_ASSERT(state_ == state_t::msg_first);
    if (msg.get_kind() != kind::ready_for_query)
        return error_code(client_errc::unexpected_message);
    return advance();
}

read_response_fsm_impl::result read_response_fsm_impl::handle_query(const any_backend_message& msg)
{
    // either
    //    at least one
    //        optional row_description (we synthesize one of not present)
    //        any number of data_row
    //        finalizer: command_complete, error_response
    //    ready_for_query
    // or empty_query_response
    switch (state_)
    {
        case state_t::msg_first:
        case state_t::query_first:
        {
            switch (msg.get_kind())
            {
                case kind::error_response:
                    // An error should always be followed by ReadyForQuery
                    state_ = state_t::query_needs_ready;
                    call_handler(msg.get_error_response());
                    return result_type::read;
                case kind::row_description:
                    // Row descriptions are optional, and can only appear at the beginning of
                    // a resultset, but should always precede rows
                    state_ = state_t::query_rows;
                    call_handler(msg.get_row_description());
                    return result_type::read;
                case kind::command_complete:
                    // Query might return command_complete directly, without NoData.
                    // Synthesize a fake row_description to help handlers
                    state_ = state_t::query_first;
                    call_handler(row_description{});
                    call_handler(msg.get_command_complete());
                    return result_type::read;
                case kind::empty_query_response:
                    // Only allowed as the first and only message. Signals that there was no query to begin
                    // with
                    if (state_ != state_t::msg_first)
                        return error_code(client_errc::unexpected_message);
                    state_ = state_t::query_needs_ready;
                    call_handler(msg.get_empty_query_response());
                    return result_type::read;
                case kind::ready_for_query:
                    if (state_ != state_t::query_first)
                        return error_code(client_errc::unexpected_message);
                    // Not allowed as the only response to a query
                    state_ = state_t::msg_first;
                    return advance();
                default: return error_code(client_errc::unexpected_message);
            }
        }
        case state_t::query_rows:
        {
            switch (msg.get_kind())
            {
                case kind::error_response:
                    // An error should always be followed by ReadyForQuery
                    state_ = state_t::query_needs_ready;
                    call_handler(msg.get_error_response());
                    return result_type::read;
                case kind::data_row:
                    // We got a row. This doesn't change state
                    call_handler(msg.get_data_row());
                    return result_type::read;
                case kind::command_complete:
                    // This resultset is done
                    state_ = state_t::query_first;
                    call_handler(msg.get_command_complete());
                    return result_type::read;
                default: return error_code(client_errc::unexpected_message);
            }
        }
        case state_t::query_needs_ready:
            // Only a sync is allowed here. Not even an error
            if (msg.get_kind() == kind::ready_for_query)
            {
                // We're done with the current message
                state_ = state_t::msg_first;
                return advance();
            }
            return error_code(client_errc::unexpected_message);
        default: BOOST_ASSERT(false); return error_code(client_errc::unexpected_message);
    }
}

read_response_fsm_impl::result read_response_fsm_impl::resume(const any_backend_message& msg)
{
    // Some messages may be found interleaved with the expected message flow
    // TODO: actually do something useful with these
    switch (msg.get_kind())
    {
        case kind::notice_response:
        case kind::notification_response:
        case kind::parameter_status: return result_type::read;
        default: break;
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
