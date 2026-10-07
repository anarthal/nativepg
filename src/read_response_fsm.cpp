//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <cstddef>
#include <system_error>

#include "nativepg/client_errc.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/protocol/any_backend_message.hpp"
#include "nativepg/protocol/describe.hpp"
#include "nativepg/protocol/read_response_fsm.hpp"
#include "nativepg/request.hpp"
#include "nativepg/responses/any_request_message.hpp"
#include "nativepg/responses/response_handler.hpp"

// TODO: some ErrorResponse messages contain fatal errors
// (e.g. when the server detected a desync). We should probably
// detect these here and report them.

using namespace nativepg;
using protocol::any_backend_message;
using kind = protocol::any_backend_message::kind;
using protocol::detail::read_response_fsm_impl;
using state_t = read_response_fsm_impl::state_t;

enum class read_response_fsm_impl::state_t
{
    msg_first = 0,
    query_first,
    query_needs_ready,
    query_rows,
    exec_copy_out,
    exec_copy_out_needs_command_complete,
    exec_copy_in,
    exec_copy_both,
    exec_copy_both_needs_command_complete,
    query_copy_out,
    query_copy_out_needs_command_complete,
    query_copy_in,
    query_copy_both,
    query_copy_both_needs_command_complete,
};

// Counts the number of Sync messages after an Exec/Query that has returned a CopyInResponse,
// and validates that only Flush and Sync messages follow the message.
// TODO: the restriction applies to Execute, but I think Query should be fine?
static protocol::read_response_fsm::result_type count_copy_in_syncs(const read_response_fsm_impl& fsm)
{
    std::size_t num_syncs = 0u;

    for (auto type : fsm.req->messages().subspan(fsm.current + 1u))
    {
        switch (type)
        {
            case request_message_type::sync: ++num_syncs; break;
            case request_message_type::flush: break;
            default: return {client_errc::copy_in_not_last, 0u};
        }
    }

    return {{}, num_syncs};
}

static void call_handler(read_response_fsm_impl& fsm, const any_request_message& msg)
{
    // First error wins. Pass a dummy object if there is already an error
    if (fsm.handler_err.code) [[unlikely]]
    {
        extended_error dummy;
        fsm.handler.on_message(msg, fsm.current, dummy);
    }
    else [[likely]]
    {
        fsm.handler.on_message(msg, fsm.current, fsm.handler_err);
    }
}

static std::error_code handle_error(read_response_fsm_impl& fsm, const protocol::error_response& err)
{
    // Call the handler with the error
    call_handler(fsm, err);
    ++fsm.current;
    fsm.state = state_t::msg_first;

    // Skip subsequent messages until a sync is found
    // We should always find one because we check that this is the case
    // before sending the request
    for (; fsm.current < fsm.req->messages().size(); ++fsm.current)
    {
        switch (fsm.req->messages()[fsm.current])
        {
            case request_message_type::sync: return client_errc::needs_more;
            case request_message_type::flush: break;
            default: call_handler(fsm, any_request_message::message_skipped()); break;
        }
    }

    BOOST_ASSERT(false);
    return std::error_code(client_errc::request_ends_without_sync);
}

static std::error_code advance(read_response_fsm_impl& fsm)
{
    if (++fsm.current >= fsm.req->messages().size())
        return std::error_code();
    else
        return client_errc::needs_more;
}

static std::error_code handle_bind(read_response_fsm_impl& fsm, const any_backend_message& msg)
{
    // bind: either (bind_complete, error_response)
    BOOST_ASSERT(fsm.state == state_t::msg_first);
    switch (msg.type())
    {
        case kind::error_response:
            // An error finishes this message and makes the server skip everything until sync
            return handle_error(fsm, msg.get_error_response());
        case kind::bind_complete:
            // Finishes the bind phase
            call_handler(fsm, msg.get_bind_complete());
            return advance(fsm);
        default: return std::error_code(client_errc::unexpected_message);
    }
}

static std::error_code handle_close(read_response_fsm_impl& fsm, const any_backend_message& msg)
{
    // close: either (close_complete, error_response)
    BOOST_ASSERT(fsm.state == state_t::msg_first);
    switch (msg.type())
    {
        case kind::error_response:
            // An error finishes this message and makes the server skip everything until sync
            return handle_error(fsm, msg.get_error_response());
        case kind::close_complete:
            // Finishes the close phase
            call_handler(fsm, msg.get_close_complete());
            return advance(fsm);
        default: return std::error_code(client_errc::unexpected_message);
    }
}

// TODO: we need to differentiate between describe statement and portal
// only the portal version is supported now
static std::error_code handle_describe(read_response_fsm_impl& fsm, const any_backend_message& msg)
{
    // describe (portal)
    //   either: row_description, no_data, error_response
    // describe (statement) TODO: support this. Either
    //   parameter_description, then either (row_description, no_data)
    //   error_response
    BOOST_ASSERT(fsm.state == state_t::msg_first);
    switch (msg.type())
    {
        case kind::error_response:
            // An error finishes this message and makes the server skip everything until sync
            return handle_error(fsm, msg.get_error_response());
        case kind::row_description:
            // Finishes the describe phase
            call_handler(fsm, msg.get_row_description());
            return advance(fsm);
        case kind::no_data:
            // We transform no_data into an empty row description, for uniformity.
            // Finishes the describe phase.
            call_handler(fsm, protocol::row_description{});
            return advance(fsm);
        default: return std::error_code(client_errc::unexpected_message);
    }
}

static protocol::read_response_fsm::result_type handle_execute(
    read_response_fsm_impl& fsm,
    const any_backend_message& msg
)
{
    // execute: either:
    //   copy_out_response, then any number of copy_data, then copy_done, then either (command_complete,
    //       error_response)
    //   copy_in_response, then either (command_complete, error_response). The client drives the
    //       copy_data/copy_done/copy_fail flow, so we observe nothing until it finishes
    //   copy_both_response, then any number of copy_data, then copy_done, then either (command_complete,
    //       error_response)
    //   any number of data_row, then either (command_complete, portal_suspended,
    //       error_response) empty_query_response
    switch (fsm.state)
    {
        case state_t::msg_first:
        {
            switch (msg.type())
            {
                case kind::copy_out_response:
                    // Starts a COPY OUT block. Data is delivered to the handler as copy_data
                    call_handler(fsm, msg.get_copy_out_response());
                    fsm.state = state_t::exec_copy_out;
                    return {client_errc::needs_more, 0u};
                case kind::copy_in_response:
                {
                    // Starts a COPY IN block. The client supplies the data, so we don't
                    // expect anything from the server until the copy finishes
                    auto [ec, nsyncs] = count_copy_in_syncs(fsm);
                    if (ec)
                        return {ec, 0u};
                    call_handler(fsm, msg.get_copy_in_response());
                    fsm.state = state_t::exec_copy_in;
                    return {client_errc::copy_in, nsyncs};
                }
                case kind::copy_both_response:
                    // Starts a COPY BOTH block. Both sides send data
                    call_handler(fsm, msg.get_copy_both_response());
                    fsm.state = state_t::exec_copy_both;
                    return {client_errc::needs_more, 0u};
                case kind::error_response:
                    // An error finishes this message and makes the server skip everything until sync
                    return {handle_error(fsm, msg.get_error_response()), 0u};
                case kind::command_complete:
                    // Finishes the execution phase
                    call_handler(fsm, msg.get_command_complete());
                    return {advance(fsm), 0u};
                case kind::empty_query_response:
                    // Finishes the execution phase
                    call_handler(fsm, msg.get_empty_query_response());
                    return {advance(fsm), 0u};
                case kind::portal_suspended:
                    // Finishes the execution phase
                    call_handler(fsm, msg.get_portal_suspended());
                    return {advance(fsm), 0u};
                case kind::data_row:
                    // We got a row. This doesn't change state
                    call_handler(fsm, msg.get_data_row());
                    return {client_errc::needs_more, 0u};
                default: return {client_errc::unexpected_message, 0u};
            }
        }
        case state_t::exec_copy_out:
        case state_t::exec_copy_both:
        {
            switch (msg.type())
            {
                case kind::copy_data:
                    // Deliver the data to the handler. This doesn't change state
                    // TODO: handle this like COPY IN
                    call_handler(fsm, msg.get_copy_data());
                    return {client_errc::needs_more, 0u};
                case kind::error_response:
                    // Terminates the copy
                    return {handle_error(fsm, msg.get_error_response()), 0u};
                case kind::copy_done:
                    // Terminates the copy, but should be followed by CommandComplete
                    fsm.state = fsm.state == state_t::exec_copy_out
                                    ? state_t::exec_copy_out_needs_command_complete
                                    : state_t::exec_copy_both_needs_command_complete;
                    return {client_errc::needs_more, 0u};
                default: return {client_errc::unexpected_message, 0u};
            }
        }
        case state_t::exec_copy_in:
        case state_t::exec_copy_out_needs_command_complete:
        case state_t::exec_copy_both_needs_command_complete:
        {
            switch (msg.type())
            {
                case kind::error_response:
                    // This is possible, in theory
                    return {handle_error(fsm, msg.get_error_response()), 0u};
                case kind::command_complete:
                    call_handler(fsm, msg.get_command_complete());
                    fsm.state = state_t::msg_first;
                    return {advance(fsm), 0u};
                default: return {client_errc::unexpected_message, 0u};
            }
        }
        default: return {client_errc::unexpected_message, 0u};
    }
}

static std::error_code handle_parse(read_response_fsm_impl& fsm, const any_backend_message& msg)
{
    // parse: either (parse_complete, error_response)
    BOOST_ASSERT(fsm.state == state_t::msg_first);
    switch (msg.type())
    {
        case kind::error_response:
            // An error finishes this message and makes the server skip everything until sync
            return handle_error(fsm, msg.get_error_response());
        case kind::parse_complete:
            // Finishes the parse phase
            call_handler(fsm, msg.get_parse_complete());
            return advance(fsm);
        default: return std::error_code(client_errc::unexpected_message);
    }
}

static std::error_code handle_sync(read_response_fsm_impl& fsm, const any_backend_message& msg)
{
    // sync always returns ReadyForQuery. Getting an error here is a protocol error,
    // as we don't know whether the connection is healthy or not
    BOOST_ASSERT(fsm.state == state_t::msg_first);
    if (msg.type() != kind::ready_for_query)
        return std::error_code(client_errc::unexpected_message);
    return advance(fsm);
}

static protocol::read_response_fsm::result_type handle_query(
    read_response_fsm_impl& fsm,
    const any_backend_message& msg
)
{
    // either
    //    at least one
    //        either
    //            optional row_description (we synthesize one of not present)
    //            any number of data_row
    //            finalizer: command_complete, error_response
    //        or
    //            copy_out_response or copy_both_response
    //            any number of copy_data
    //            copy_done, followed by (command_complete or error_response), or error_response
    //        or
    //            copy_in_response, followed by (command_complete or error_response).
    //            The client drives the copy_data/copy_done/copy_fail flow
    //    ready_for_query
    // or empty_query_response
    switch (fsm.state)
    {
        case state_t::msg_first:
        case state_t::query_first:
        {
            switch (msg.type())
            {
                case kind::copy_out_response:
                    // Starts a COPY OUT block. Data is delivered to the handler as copy_data
                    call_handler(fsm, msg.get_copy_out_response());
                    fsm.state = state_t::query_copy_out;
                    return {client_errc::needs_more, 0u};
                case kind::copy_in_response:
                {
                    // Starts a COPY IN block. The client supplies the data, so we don't
                    // expect anything from the server until the copy finishes.
                    // The simple query protocol has no Sync messages, so the server
                    // discards none of them and the client has none to re-send
                    auto [ec, nsyncs] = count_copy_in_syncs(fsm);
                    if (ec)
                        return {ec, 0u};
                    call_handler(fsm, msg.get_copy_in_response());
                    fsm.state = state_t::query_copy_in;
                    return {client_errc::copy_in, nsyncs};
                }
                case kind::copy_both_response:
                    // Starts a COPY BOTH block. Both sides send data
                    call_handler(fsm, msg.get_copy_both_response());
                    fsm.state = state_t::query_copy_both;
                    return {client_errc::needs_more, 0u};
                case kind::error_response:
                    // An error should always be followed by ReadyForQuery
                    fsm.state = state_t::query_needs_ready;
                    call_handler(fsm, msg.get_error_response());
                    return {client_errc::needs_more, 0u};
                case kind::row_description:
                    // Row descriptions are optional, and can only appear at the beginning of
                    // a resultset, but should always precede rows
                    fsm.state = state_t::query_rows;
                    call_handler(fsm, msg.get_row_description());
                    return {client_errc::needs_more, 0u};
                case kind::command_complete:
                    // Query might return command_complete directly, without NoData.
                    // Synthesize a fake row_description to help handlers
                    fsm.state = state_t::query_first;
                    call_handler(fsm, protocol::row_description{});
                    call_handler(fsm, msg.get_command_complete());
                    return {client_errc::needs_more, 0u};
                case kind::empty_query_response:
                    // Only allowed as the first and only message. Signals that there was no query to begin
                    // with
                    if (fsm.state != state_t::msg_first)
                        return {client_errc::unexpected_message, 0u};
                    fsm.state = state_t::query_needs_ready;
                    call_handler(fsm, msg.get_empty_query_response());
                    return {client_errc::needs_more, 0u};
                case kind::ready_for_query:
                    if (fsm.state != state_t::query_first)
                        return {client_errc::unexpected_message, 0u};
                    // Not allowed as the only response to a query
                    fsm.state = state_t::msg_first;
                    return {advance(fsm), 0u};
                default: return {client_errc::unexpected_message, 0u};
            }
        }
        case state_t::query_rows:
        {
            switch (msg.type())
            {
                case kind::error_response:
                    // An error should always be followed by ReadyForQuery
                    fsm.state = state_t::query_needs_ready;
                    call_handler(fsm, msg.get_error_response());
                    return {client_errc::needs_more, 0u};
                case kind::data_row:
                    // We got a row. This doesn't change state
                    call_handler(fsm, msg.get_data_row());
                    return {client_errc::needs_more, 0u};
                case kind::command_complete:
                    // This resultset is done
                    fsm.state = state_t::query_first;
                    call_handler(fsm, msg.get_command_complete());
                    return {client_errc::needs_more, 0u};
                default: return {client_errc::unexpected_message, 0u};
            }
        }
        case state_t::query_needs_ready:
            // Only a sync is allowed here. Not even an error
            if (msg.type() == kind::ready_for_query)
            {
                // We're done with the current message
                fsm.state = state_t::msg_first;
                return {advance(fsm), 0u};
            }
            return {client_errc::unexpected_message, 0u};
        case state_t::query_copy_out:
        case state_t::query_copy_both:
        {
            switch (msg.type())
            {
                case kind::copy_data:
                    // Deliver the data to the handler. This doesn't change state
                    call_handler(fsm, msg.get_copy_data());
                    return {client_errc::needs_more, 0u};
                case kind::error_response:
                    // An error should always be followed by ReadyForQuery
                    fsm.state = state_t::query_needs_ready;
                    call_handler(fsm, msg.get_error_response());
                    return {client_errc::needs_more, 0u};
                case kind::copy_done:
                    // Terminates the copy, but should be followed by CommandComplete
                    fsm.state = fsm.state == state_t::query_copy_out
                                    ? state_t::query_copy_out_needs_command_complete
                                    : state_t::query_copy_both_needs_command_complete;
                    return {client_errc::needs_more, 0u};
                default: return {client_errc::unexpected_message, 0u};
            }
        }
        case state_t::query_copy_in:
        case state_t::query_copy_out_needs_command_complete:
        case state_t::query_copy_both_needs_command_complete:
        {
            switch (msg.type())
            {
                case kind::error_response:
                    // An error should always be followed by ReadyForQuery
                    fsm.state = state_t::query_needs_ready;
                    call_handler(fsm, msg.get_error_response());
                    return {client_errc::needs_more, 0u};
                case kind::command_complete:
                    call_handler(fsm, msg.get_command_complete());
                    fsm.state = state_t::query_first;
                    return {client_errc::needs_more, 0u};
                default: return {client_errc::unexpected_message, 0u};
            }
        }
        default: BOOST_ASSERT(false); return {client_errc::unexpected_message, 0u};
    }
}

protocol::read_response_fsm::result_type protocol::read_response_fsm::resume(const any_backend_message& msg)
{
    // Some messages may be found interleaved with the expected message flow
    // TODO: actually do something useful with these
    switch (msg.type())
    {
        case kind::notice_response:
        case kind::notification_response:
        case kind::parameter_status: return {client_errc::needs_more, 0u};
        default: break;
    }

    // The allowed messages depend on the current type
    while (true)
    {
        switch (impl_.req->messages()[impl_.current])
        {
            case request_message_type::bind: return {handle_bind(impl_, msg), 0u};
            case request_message_type::close: return {handle_close(impl_, msg), 0u};
            case request_message_type::describe: return {handle_describe(impl_, msg), 0u};
            case request_message_type::execute: return handle_execute(impl_, msg);
            case request_message_type::flush:
            {
                ++impl_.current;
                continue;  // nothing is expected here
            }
            case request_message_type::parse: return {handle_parse(impl_, msg), 0u};
            case request_message_type::query: return handle_query(impl_, msg);
            case request_message_type::sync: return {handle_sync(impl_, msg), 0u};
        }
    }
}
