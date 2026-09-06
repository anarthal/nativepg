//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/assert/source_location.hpp>
#include <boost/core/lightweight_test.hpp>

#include <cstddef>
#include <initializer_list>
#include <iostream>
#include <ostream>
#include <string>
#include <system_error>
#include <vector>

#include "nativepg/client_errc.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/protocol/async.hpp"
#include "nativepg/protocol/bind.hpp"
#include "nativepg/protocol/close.hpp"
#include "nativepg/protocol/command_complete.hpp"
#include "nativepg/protocol/data_row.hpp"
#include "nativepg/protocol/describe.hpp"
#include "nativepg/protocol/empty_query_response.hpp"
#include "nativepg/protocol/execute.hpp"
#include "nativepg/protocol/notice_error.hpp"
#include "nativepg/protocol/parse.hpp"
#include "nativepg/protocol/read_response_fsm.hpp"
#include "nativepg/protocol/ready_for_query.hpp"
#include "nativepg/protocol/sync.hpp"
#include "nativepg/request.hpp"
#include "nativepg/responses/any_request_message.hpp"
#include "nativepg/responses/response_handler.hpp"
#include "test_utils/printing.hpp"
#include "test_utils/response_handler_utils.hpp"

using namespace nativepg;
using namespace nativepg::test;
using protocol::read_response_fsm;
using std::error_code;
using kind = any_request_message::kind;

namespace {

const error_code needs_more{client_errc::needs_more};

// Some distinct errors for the handler to report
extended_error first_error() { return {client_errc::field_not_found, diagnostics(std::string("first"))}; }
extended_error second_error()
{
    return {client_errc::incompatible_field_type, diagnostics(std::string("second"))};
}

// A handler that stores its arguments, and optionally reports errors
struct mock_handler
{
    std::vector<on_msg_args> msgs;

    // What the handler reports on each successive on_message() call.
    // A missing entry means "no error"
    std::vector<extended_error> errors;

    handler_setup_result setup(const request&, std::size_t offset) { return {offset}; }
    void on_message(const any_request_message& msg, std::size_t offset, extended_error& err)
    {
        // Regression check: the passed error is always clean
        BOOST_TEST_EQ(err, extended_error{});

        const std::size_t i = msgs.size();
        msgs.push_back({msg.type(), offset});
        if (i < errors.size())
            err = errors[i];
    }
};

struct fixture
{
    request req;
    mock_handler handler;
    read_response_fsm fsm{&req, &handler};

    void check(
        std::initializer_list<on_msg_args> expected,
        boost::source_location loc = BOOST_CURRENT_LOCATION
    )
    {
        if (!BOOST_TEST_ALL_EQ(handler.msgs.begin(), handler.msgs.end(), expected.begin(), expected.end()))
            std::cerr << "Called from " << loc << std::endl;
    }
};

// --- Responses to a simple query ---
void test_simple_query()
{
    fixture fix;
    fix.req.add_simple_query("SELECT 1");

    // Run the FSM
    BOOST_TEST_EQ(fix.fsm.resume(protocol::row_description{}), needs_more);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::data_row{}), needs_more);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::data_row{}), needs_more);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::command_complete{}), needs_more);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::ready_for_query{}), error_code());

    // Check handler messages
    fix.check({
        {kind::row_description,  0u},
        {kind::data_row,         0u},
        {kind::data_row,         0u},
        {kind::command_complete, 0u},
    });
}

// Queries may return no rows
void test_simple_query_no_rows()
{
    fixture fix;
    fix.req.add_simple_query("SELECT 1");

    // Run the FSM
    BOOST_TEST_EQ(fix.fsm.resume(protocol::row_description{}), needs_more);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::command_complete{}), needs_more);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::ready_for_query{}), error_code());

    // Check handler messages
    fix.check({
        {kind::row_description,  0u},
        {kind::command_complete, 0u},
    });
}

// Queries that don't return data don't include a row_description.
// We synthesize an empty row_description for simplicity
void test_simple_query_no_data()
{
    fixture fix;
    fix.req.add_simple_query("SELECT 1");

    // Run the FSM
    BOOST_TEST_EQ(fix.fsm.resume(protocol::command_complete{}), needs_more);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::ready_for_query{}), error_code());

    // Check handler messages
    fix.check({
        {kind::row_description,  0u},
        {kind::command_complete, 0u},
    });
}

// Queries may contain an arbitrary number of resultsets, some of them empty
void test_simple_query_multi()
{
    fixture fix;
    fix.req.add_simple_query("SELECT 1");

    // Run the FSM
    BOOST_TEST_EQ(fix.fsm.resume(protocol::row_description{}), needs_more);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::data_row{}), needs_more);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::data_row{}), needs_more);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::command_complete{}), needs_more);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::command_complete{}), needs_more);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::row_description{}), needs_more);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::command_complete{}), needs_more);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::ready_for_query{}), error_code());

    // Check handler messages
    fix.check({
        {kind::row_description,  0u},
        {kind::data_row,         0u},
        {kind::data_row,         0u},
        {kind::command_complete, 0u},
        {kind::row_description,  0u},
        {kind::command_complete, 0u},
        {kind::row_description,  0u},
        {kind::command_complete, 0u},
    });
}

// A query might return a single EmptyQueryResponse, finishing the query sequence
void test_simple_query_empty()
{
    fixture fix;
    fix.req.add_simple_query("");

    // Run the FSM
    BOOST_TEST_EQ(fix.fsm.resume(protocol::empty_query_response{}), needs_more);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::ready_for_query{}), error_code());

    // Check handler messages
    fix.check({
        {kind::empty_query_response, 0u},
    });
}

// A query might return an error
void test_simple_query_error()
{
    fixture fix;
    fix.req.add_simple_query("SELECT 1");

    // Run the FSM
    BOOST_TEST_EQ(fix.fsm.resume(protocol::error_response{}), needs_more);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::ready_for_query{}), error_code());

    // Check handler messages
    fix.check({
        {kind::error_response, 0u},
    });
}

// Query errors don't trigger any skipping
void test_simple_query_error_skipping()
{
    fixture fix;
    fix.req.add_simple_query("SELECT 1").add_simple_query("SELECT 2");

    // Run the FSM
    BOOST_TEST_EQ(fix.fsm.resume(protocol::error_response{}), needs_more);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::ready_for_query{}), needs_more);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::command_complete{}), needs_more);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::ready_for_query{}), error_code());

    // Check handler messages
    fix.check({
        {kind::error_response,   0u},
        {kind::row_description,  1u},
        {kind::command_complete, 1u},
    });
}

// TODO: test more combinations here

// --- Responses to parse ---
void test_parse()
{
    fixture fix;
    fix.req.add_prepare("SELECT 1", "mystmt");

    // Run the FSM
    BOOST_TEST_EQ(fix.fsm.resume(protocol::parse_complete{}), needs_more);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::ready_for_query{}), error_code());

    // Check handler messages
    fix.check({
        {kind::parse_complete, 0u},
    });
}

// Parse might return an error
// Parse errors trigger skipping of subsequent messages
void test_parse_error()
{
    fixture fix;
    fix.req.set_autosync(false);
    fix.req.add_prepare("SELECT 1", "mystmt").add_prepare("SELECT 2", "stmt").add(protocol::sync{});

    // Run the FSM
    BOOST_TEST_EQ(fix.fsm.resume(protocol::error_response{}), needs_more);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::ready_for_query{}), error_code());

    // Check handler messages
    fix.check({
        {kind::error_response,  0u},
        {kind::message_skipped, 1u},
    });
}

// --- Responses to bind ---
void test_bind()
{
    fixture fix;
    fix.req.add(protocol::bind{}).add(protocol::sync{});

    // Run the FSM
    BOOST_TEST_EQ(fix.fsm.resume(protocol::bind_complete{}), needs_more);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::ready_for_query{}), error_code());

    // Check handler messages
    fix.check({
        {kind::bind_complete, 0u},
    });
}

// Bind might return an error, triggering skipping
void test_bind_error()
{
    fixture fix;
    fix.req.add(protocol::bind{}).add(protocol::close{}).add(protocol::sync{});

    // Run the FSM
    BOOST_TEST_EQ(fix.fsm.resume(protocol::error_response{}), needs_more);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::ready_for_query{}), error_code());

    // Check handler messages
    fix.check({
        {kind::error_response,  0u},
        {kind::message_skipped, 1u},
    });
}

// --- Responses to execute ---
void test_execute()
{
    fixture fix;
    fix.req.add(protocol::execute{}).add(protocol::sync{});

    // Run the FSM
    BOOST_TEST_EQ(fix.fsm.resume(protocol::data_row{}), needs_more);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::data_row{}), needs_more);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::command_complete{}), needs_more);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::ready_for_query{}), error_code());

    // Check handler messages
    fix.check({
        {kind::data_row,         0u},
        {kind::data_row,         0u},
        {kind::command_complete, 0u},
    });
}

// Execute might return no rows
void test_execute_no_rows()
{
    fixture fix;
    fix.req.add(protocol::execute{}).add(protocol::sync{});

    // Run the FSM
    BOOST_TEST_EQ(fix.fsm.resume(protocol::command_complete{}), needs_more);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::ready_for_query{}), error_code());

    // Check handler messages
    fix.check({
        {kind::command_complete, 0u},
    });
}

// Execute might return portal_suspended to signal that we have more rows to read
void test_execute_portal_suspended()
{
    fixture fix;
    fix.req.add(protocol::execute{}).add(protocol::sync{});

    // Run the FSM
    BOOST_TEST_EQ(fix.fsm.resume(protocol::data_row{}), needs_more);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::data_row{}), needs_more);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::portal_suspended{}), needs_more);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::ready_for_query{}), error_code());

    // Check handler messages
    fix.check({
        {kind::data_row,         0u},
        {kind::data_row,         0u},
        {kind::portal_suspended, 0u},
    });
}

// Execute might return EmptyQueryResponse to signal that we passed an empty query
void test_execute_empty()
{
    fixture fix;
    fix.req.add(protocol::execute{}).add(protocol::sync{});

    // Run the FSM
    BOOST_TEST_EQ(fix.fsm.resume(protocol::empty_query_response{}), needs_more);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::ready_for_query{}), error_code());

    // Check handler messages
    fix.check({
        {kind::empty_query_response, 0u},
    });
}

// Execute might return an error, triggering skipping
void test_execute_error()
{
    fixture fix;
    fix.req.add(protocol::execute{}).add(protocol::execute{}).add(protocol::sync{});

    // Run the FSM
    BOOST_TEST_EQ(fix.fsm.resume(protocol::error_response{}), needs_more);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::ready_for_query{}), error_code());

    // Check handler messages
    fix.check({
        {kind::error_response,  0u},
        {kind::message_skipped, 1u},
    });
}

// --- Responses to describe portal ---
void test_describe_portal()
{
    fixture fix;
    fix.req.add_describe_portal("abc");

    // Run the FSM
    BOOST_TEST_EQ(fix.fsm.resume(protocol::row_description{}), needs_more);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::ready_for_query{}), error_code());

    // Check handler messages
    fix.check({
        {kind::row_description, 0u},
    });
}

// Describe portal might return no_data to indicate that the query returns no data.
// We translate this into an empty row_description
void test_describe_portal_no_data()
{
    fixture fix;
    fix.req.add_describe_portal("abc");

    // Run the FSM
    BOOST_TEST_EQ(fix.fsm.resume(protocol::no_data{}), needs_more);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::ready_for_query{}), error_code());

    // Check handler messages
    fix.check({
        {kind::row_description, 0u},
    });
}

// Describe portal might return an error, triggering skipping
void test_describe_portal_error()
{
    fixture fix;
    fix.req.set_autosync(false);
    fix.req.add_describe_portal("abc").add_describe_portal("def").add(protocol::sync{});

    // Run the FSM
    BOOST_TEST_EQ(fix.fsm.resume(protocol::error_response{}), needs_more);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::ready_for_query{}), error_code());

    // Check handler messages
    fix.check({
        {kind::error_response,  0u},
        {kind::message_skipped, 1u},
    });
}

// --- Responses to close ---
void test_close()
{
    fixture fix;
    fix.req.add_close_statement("abc");

    // Run the FSM
    BOOST_TEST_EQ(fix.fsm.resume(protocol::close_complete{}), needs_more);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::ready_for_query{}), error_code());

    // Check handler messages
    fix.check({
        {kind::close_complete, 0u},
    });
}

// Close might return an error, triggering skipping
void test_close_error()
{
    fixture fix;
    fix.req.set_autosync(false);
    fix.req.add_close_statement("abc").add_close_statement("def").add(protocol::sync{});

    // Run the FSM
    BOOST_TEST_EQ(fix.fsm.resume(protocol::error_response{}), needs_more);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::ready_for_query{}), error_code());

    // Check handler messages
    fix.check({
        {kind::error_response,  0u},
        {kind::message_skipped, 1u},
    });
}

// --- Pipeline cases ---
// The usual extended query flow works
void test_extended_query()
{
    fixture fix;
    fix.req.add_query("SELECT 1");

    // Run the FSM
    BOOST_TEST_EQ(fix.fsm.resume(protocol::parse_complete{}), needs_more);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::bind_complete{}), needs_more);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::row_description{}), needs_more);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::data_row{}), needs_more);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::command_complete{}), needs_more);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::ready_for_query{}), error_code());

    // Check handler messages
    fix.check({
        {kind::parse_complete,   0u},
        {kind::bind_complete,    1u},
        {kind::row_description,  2u},
        {kind::data_row,         3u},
        {kind::command_complete, 3u},
    });
}

// Async messages (notices, notifications, parameter descriptions) are ignored
void test_async()
{
    fixture fix;
    fix.req.add_prepare("SELECT 1", "mystmt");

    // Run the FSM
    BOOST_TEST_EQ(fix.fsm.resume(protocol::parse_complete{}), needs_more);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::notice_response{}), needs_more);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::notification_response{}), needs_more);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::parameter_status{}), needs_more);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::ready_for_query{}), error_code());

    // Check handler messages
    fix.check({
        {kind::parse_complete, 0u}
    });
}

// Pipeline that contains several syncs
void test_several_syncs()
{
    fixture fix;
    fix.req.add_close_statement("abc");
    fix.req.add_query("SELECT 1");
    fix.req.add_describe_portal("def");

    // Run the FSM
    BOOST_TEST_EQ(fix.fsm.resume(protocol::close_complete{}), needs_more);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::ready_for_query{}), needs_more);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::parse_complete{}), needs_more);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::bind_complete{}), needs_more);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::row_description{}), needs_more);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::data_row{}), needs_more);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::command_complete{}), needs_more);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::ready_for_query{}), needs_more);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::row_description{}), needs_more);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::ready_for_query{}), error_code());

    // Check handler messages
    fix.check({
        {kind::close_complete,   0u},
        {kind::parse_complete,   2u},
        {kind::bind_complete,    3u},
        {kind::row_description,  4u},
        {kind::data_row,         5u},
        {kind::command_complete, 5u},
        {kind::row_description,  7u},
    });
}

// Error skipping ends when we encounter a sync
void test_error_recovery()
{
    fixture fix;
    fix.req.add_close_statement("abc");
    fix.req.add_query("SELECT 1");
    fix.req.add_describe_portal("def");

    // Run the FSM
    BOOST_TEST_EQ(fix.fsm.resume(protocol::close_complete{}), needs_more);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::ready_for_query{}), needs_more);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::parse_complete{}), needs_more);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::error_response{}), needs_more);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::ready_for_query{}), needs_more);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::row_description{}), needs_more);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::ready_for_query{}), error_code());

    // Check handler messages
    fix.check({
        {kind::close_complete,  0u},
        {kind::parse_complete,  2u},
        {kind::error_response,  3u},
        {kind::message_skipped, 4u},
        {kind::message_skipped, 5u},
        {kind::row_description, 7u},
    });
}

// If the sync is the last message, we handle it correctly
void test_error_recovery_sync_last()
{
    fixture fix;
    fix.req.add_close_statement("abc");

    // Run the FSM
    BOOST_TEST_EQ(fix.fsm.resume(protocol::error_response{}), needs_more);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::ready_for_query{}), error_code());

    // Check handler messages
    fix.check({
        {kind::error_response, 0u},
    });
}

// --- Errors reported by the handler ---

// An error reported by the handler does not cause the pipeline to fail,
// but is reported in get_handler_error()
// Also tests that an error in the last message doesn't cause trouble.
void test_handler_error_does_not_fail_fsm()
{
    fixture fix;
    fix.req.add_simple_query("SELECT 1");
    fix.handler.errors = {{}, first_error()};  // fails when handed the command_complete

    // The FSM runs to completion, unaffected
    BOOST_TEST_EQ(fix.fsm.resume(protocol::row_description{}), needs_more);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::command_complete{}), needs_more);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::ready_for_query{}), error_code());

    // The handler's error is reported separately
    BOOST_TEST_EQ(fix.fsm.get_handler_error(), first_error());

    // Every message still reached the handler, including the ones after the failure
    fix.check({
        {kind::row_description,  0u},
        {kind::command_complete, 0u},
    });
}

// A success after a failure doesn't clear the error we already recorded
void test_handler_error_then_nonerror()
{
    fixture fix;
    fix.req.add_simple_query("SELECT 1");
    fix.handler.errors = {first_error(), {}, {}};  // fails on the row_description only

    BOOST_TEST_EQ(fix.fsm.resume(protocol::row_description{}), needs_more);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::data_row{}), needs_more);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::command_complete{}), needs_more);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::ready_for_query{}), error_code());

    BOOST_TEST_EQ(fix.fsm.get_handler_error(), first_error());

    fix.check({
        {kind::row_description,  0u},
        {kind::data_row,         0u},
        {kind::command_complete, 0u},
    });
}

// Only the first error is retained
// Regression check for a bug that caused the 3rd error to pass
// a dirty extended_error into the handler
void test_handler_three_errors()
{
    fixture fix;
    fix.req.add_simple_query("SELECT 1");
    fix.handler.errors = {first_error(), second_error(), second_error()};

    BOOST_TEST_EQ(fix.fsm.resume(protocol::row_description{}), needs_more);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::data_row{}), needs_more);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::command_complete{}), needs_more);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::ready_for_query{}), error_code());

    BOOST_TEST_EQ(fix.fsm.get_handler_error(), first_error());

    fix.check({
        {kind::row_description,  0u},
        {kind::data_row,         0u},
        {kind::command_complete, 0u},
    });
}

// Errors detected by the FSM don't end up in get_handler_error()
void test_fsm_error_not_reported_as_handler_error()
{
    fixture fix;
    fix.req.add_prepare("SELECT 1", "stmt");

    // A data_row is not a legal response to a parse
    BOOST_TEST_EQ(fix.fsm.resume(protocol::data_row{}), error_code(client_errc::unexpected_message));

    // The handler never ran, so its error is still clean
    BOOST_TEST_EQ(fix.fsm.get_handler_error(), extended_error{});
    fix.check({});
}

// TODO: test combining simple queries and extended queries
// TODO: test flush

}  // namespace

int main()
{
    test_simple_query();
    test_simple_query_no_rows();
    test_simple_query_no_data();
    test_simple_query_multi();
    test_simple_query_empty();
    test_simple_query_error();
    test_simple_query_error_skipping();

    test_parse();
    test_parse_error();

    test_bind();
    test_bind_error();

    test_execute();
    test_execute_no_rows();
    test_execute_empty();
    test_execute_portal_suspended();
    test_execute_error();

    test_describe_portal();
    test_describe_portal_no_data();
    test_describe_portal_error();

    test_close();
    test_close_error();

    test_extended_query();
    test_async();
    test_several_syncs();
    test_error_recovery();
    test_error_recovery_sync_last();

    test_handler_error_does_not_fail_fsm();
    test_handler_error_then_nonerror();
    test_handler_three_errors();
    test_fsm_error_not_reported_as_handler_error();

    return boost::report_errors();
}
