//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/assert/source_location.hpp>
#include <boost/core/lightweight_test.hpp>
#include <boost/system/detail/error_code.hpp>
#include <boost/system/error_code.hpp>
#include <boost/variant2/variant.hpp>

#include <cstddef>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <ostream>
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
#include "nativepg/response_handler.hpp"
#include "response_msg_type.hpp"

using namespace nativepg;
using namespace nativepg::test;
using boost::system::error_code;
using protocol::detail::read_response_fsm_impl;
using result_type = read_response_fsm_impl::result_type;

// TODO: rename this file when we implement the external API tests

// Operators
static const char* to_string(result_type t)
{
    switch (t)
    {
        case result_type::done: return "done";
        case result_type::read: return "read";
        default: return "<unknown result_type>";
    }
}

namespace nativepg::protocol::detail {

bool operator==(const read_response_fsm_impl::result& lhs, const read_response_fsm_impl::result& rhs)
{
    return lhs.type == rhs.type && lhs.ec == rhs.ec;
}

std::ostream& operator<<(std::ostream& os, const read_response_fsm_impl::result& value)
{
    os << "read_response_fsm_impl::result{ .type=" << to_string(value.type);
    if (value.type == result_type::done)
        os << ", .ec=" << value.ec;
    return os << " }";
}

}  // namespace nativepg::protocol::detail

namespace {

// A handler that just stores its arguments
struct mock_handler
{
    std::vector<on_msg_args> msgs;
    extended_error err;

    handler_setup_result setup(const request&, std::size_t offset) { return {offset}; }
    void on_message(const any_request_message& msg, std::size_t offset)
    {
        msgs.push_back({to_type(msg), offset});
    }
    const extended_error& result() const { return err; }
};

struct fixture
{
    request req;
    mock_handler handler;
    read_response_fsm_impl fsm{req, handler};

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
    BOOST_TEST_EQ(fix.fsm.resume(protocol::row_description{}), result_type::read);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::data_row{}), result_type::read);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::data_row{}), result_type::read);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::command_complete{}), result_type::read);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::ready_for_query{}), error_code());

    // Check handler messages
    fix.check({
        {response_msg_type::row_description,  0u},
        {response_msg_type::data_row,         0u},
        {response_msg_type::data_row,         0u},
        {response_msg_type::command_complete, 0u},
    });
}

// Queries may return no rows
void test_simple_query_no_rows()
{
    fixture fix;
    fix.req.add_simple_query("SELECT 1");

    // Run the FSM
    BOOST_TEST_EQ(fix.fsm.resume(protocol::row_description{}), result_type::read);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::command_complete{}), result_type::read);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::ready_for_query{}), error_code());

    // Check handler messages
    fix.check({
        {response_msg_type::row_description,  0u},
        {response_msg_type::command_complete, 0u},
    });
}

// Queries that don't return data don't include a row_description.
// We synthesize an empty row_description for simplicity
void test_simple_query_no_data()
{
    fixture fix;
    fix.req.add_simple_query("SELECT 1");

    // Run the FSM
    BOOST_TEST_EQ(fix.fsm.resume(protocol::command_complete{}), result_type::read);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::ready_for_query{}), error_code());

    // Check handler messages
    fix.check({
        {response_msg_type::row_description,  0u},
        {response_msg_type::command_complete, 0u},
    });
}

// Queries may contain an arbitrary number of resultsets, some of them empty
void test_simple_query_multi()
{
    fixture fix;
    fix.req.add_simple_query("SELECT 1");

    // Run the FSM
    BOOST_TEST_EQ(fix.fsm.resume(protocol::row_description{}), result_type::read);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::data_row{}), result_type::read);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::data_row{}), result_type::read);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::command_complete{}), result_type::read);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::command_complete{}), result_type::read);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::row_description{}), result_type::read);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::command_complete{}), result_type::read);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::ready_for_query{}), error_code());

    // Check handler messages
    fix.check({
        {response_msg_type::row_description,  0u},
        {response_msg_type::data_row,         0u},
        {response_msg_type::data_row,         0u},
        {response_msg_type::command_complete, 0u},
        {response_msg_type::row_description,  0u},
        {response_msg_type::command_complete, 0u},
        {response_msg_type::row_description,  0u},
        {response_msg_type::command_complete, 0u},
    });
}

// A query might return a single EmptyQueryResponse, finishing the query sequence
void test_simple_query_empty()
{
    fixture fix;
    fix.req.add_simple_query("");

    // Run the FSM
    BOOST_TEST_EQ(fix.fsm.resume(protocol::empty_query_response{}), result_type::read);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::ready_for_query{}), error_code());

    // Check handler messages
    fix.check({
        {response_msg_type::empty_query_response, 0u},
    });
}

// A query might return an error
void test_simple_query_error()
{
    fixture fix;
    fix.req.add_simple_query("SELECT 1");

    // Run the FSM
    BOOST_TEST_EQ(fix.fsm.resume(protocol::error_response{}), result_type::read);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::ready_for_query{}), error_code());

    // Check handler messages
    fix.check({
        {response_msg_type::error_response, 0u},
    });
}

// Query errors don't trigger any skipping
void test_simple_query_error_skipping()
{
    fixture fix;
    fix.req.add_simple_query("SELECT 1").add_simple_query("SELECT 2");

    // Run the FSM
    BOOST_TEST_EQ(fix.fsm.resume(protocol::error_response{}), result_type::read);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::ready_for_query{}), result_type::read);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::command_complete{}), result_type::read);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::ready_for_query{}), error_code());

    // Check handler messages
    fix.check({
        {response_msg_type::error_response,   0u},
        {response_msg_type::row_description,  1u},
        {response_msg_type::command_complete, 1u},
    });
}

// TODO: test more combinations here

// --- Responses to parse ---
void test_parse()
{
    fixture fix;
    fix.req.add_prepare("SELECT 1", "mystmt");

    // Run the FSM
    BOOST_TEST_EQ(fix.fsm.resume(protocol::parse_complete{}), result_type::read);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::ready_for_query{}), error_code());

    // Check handler messages
    fix.check({
        {response_msg_type::parse_complete, 0u},
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
    BOOST_TEST_EQ(fix.fsm.resume(protocol::error_response{}), result_type::read);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::ready_for_query{}), error_code());

    // Check handler messages
    fix.check({
        {response_msg_type::error_response,  0u},
        {response_msg_type::message_skipped, 1u},
    });
}

// --- Responses to bind ---
void test_bind()
{
    fixture fix;
    fix.req.add_bind("stmt", {}).add(protocol::sync{});

    // Run the FSM
    BOOST_TEST_EQ(fix.fsm.resume(protocol::bind_complete{}), result_type::read);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::ready_for_query{}), error_code());

    // Check handler messages
    fix.check({
        {response_msg_type::bind_complete, 0u},
    });
}

// Bind might return an error, triggering skipping
void test_bind_error()
{
    fixture fix;
    fix.req.add_bind("stmt", {}).add(protocol::close{}).add(protocol::sync{});

    // Run the FSM
    BOOST_TEST_EQ(fix.fsm.resume(protocol::error_response{}), result_type::read);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::ready_for_query{}), error_code());

    // Check handler messages
    fix.check({
        {response_msg_type::error_response,  0u},
        {response_msg_type::message_skipped, 1u},
    });
}

// --- Responses to execute ---
void test_execute()
{
    fixture fix;
    fix.req.add(protocol::execute{}).add(protocol::sync{});

    // Run the FSM
    BOOST_TEST_EQ(fix.fsm.resume(protocol::data_row{}), result_type::read);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::data_row{}), result_type::read);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::command_complete{}), result_type::read);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::ready_for_query{}), error_code());

    // Check handler messages
    fix.check({
        {response_msg_type::data_row,         0u},
        {response_msg_type::data_row,         0u},
        {response_msg_type::command_complete, 0u},
    });
}

// Execute might return no rows
void test_execute_no_rows()
{
    fixture fix;
    fix.req.add(protocol::execute{}).add(protocol::sync{});

    // Run the FSM
    BOOST_TEST_EQ(fix.fsm.resume(protocol::command_complete{}), result_type::read);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::ready_for_query{}), error_code());

    // Check handler messages
    fix.check({
        {response_msg_type::command_complete, 0u},
    });
}

// Execute might return portal_suspended to signal that we have more rows to read
void test_execute_portal_suspended()
{
    fixture fix;
    fix.req.add(protocol::execute{}).add(protocol::sync{});

    // Run the FSM
    BOOST_TEST_EQ(fix.fsm.resume(protocol::data_row{}), result_type::read);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::data_row{}), result_type::read);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::portal_suspended{}), result_type::read);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::ready_for_query{}), error_code());

    // Check handler messages
    fix.check({
        {response_msg_type::data_row,         0u},
        {response_msg_type::data_row,         0u},
        {response_msg_type::portal_suspended, 0u},
    });
}

// Execute might return EmptyQueryResponse to signal that we passed an empty query
void test_execute_empty()
{
    fixture fix;
    fix.req.add(protocol::execute{}).add(protocol::sync{});

    // Run the FSM
    BOOST_TEST_EQ(fix.fsm.resume(protocol::empty_query_response{}), result_type::read);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::ready_for_query{}), error_code());

    // Check handler messages
    fix.check({
        {response_msg_type::empty_query_response, 0u},
    });
}

// Execute might return an error, triggering skipping
void test_execute_error()
{
    fixture fix;
    fix.req.add(protocol::execute{}).add(protocol::execute{}).add(protocol::sync{});

    // Run the FSM
    BOOST_TEST_EQ(fix.fsm.resume(protocol::error_response{}), result_type::read);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::ready_for_query{}), error_code());

    // Check handler messages
    fix.check({
        {response_msg_type::error_response,  0u},
        {response_msg_type::message_skipped, 1u},
    });
}

// --- Responses to describe portal ---
void test_describe_portal()
{
    fixture fix;
    fix.req.add_describe_portal("abc");

    // Run the FSM
    BOOST_TEST_EQ(fix.fsm.resume(protocol::row_description{}), result_type::read);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::ready_for_query{}), error_code());

    // Check handler messages
    fix.check({
        {response_msg_type::row_description, 0u},
    });
}

// Describe portal might return no_data to indicate that the query returns no data.
// We translate this into an empty row_description
void test_describe_portal_no_data()
{
    fixture fix;
    fix.req.add_describe_portal("abc");

    // Run the FSM
    BOOST_TEST_EQ(fix.fsm.resume(protocol::no_data{}), result_type::read);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::ready_for_query{}), error_code());

    // Check handler messages
    fix.check({
        {response_msg_type::row_description, 0u},
    });
}

// Describe portal might return an error, triggering skipping
void test_describe_portal_error()
{
    fixture fix;
    fix.req.set_autosync(false);
    fix.req.add_describe_portal("abc").add_describe_portal("def").add(protocol::sync{});

    // Run the FSM
    BOOST_TEST_EQ(fix.fsm.resume(protocol::error_response{}), result_type::read);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::ready_for_query{}), error_code());

    // Check handler messages
    fix.check({
        {response_msg_type::error_response,  0u},
        {response_msg_type::message_skipped, 1u},
    });
}

// Response to an extended query
void test_extended_query()
{
    fixture fix;
    fix.req.add_query("SELECT 1", {});

    // Run the FSM
    BOOST_TEST_EQ(fix.fsm.resume(protocol::parse_complete{}), result_type::read);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::bind_complete{}), result_type::read);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::row_description{}), result_type::read);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::data_row{}), result_type::read);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::command_complete{}), result_type::read);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::ready_for_query{}), error_code());

    // Check handler messages
    fix.check({
        {response_msg_type::parse_complete,   0u},
        {response_msg_type::bind_complete,    1u},
        {response_msg_type::row_description,  2u},
        {response_msg_type::data_row,         3u},
        {response_msg_type::command_complete, 3u},
    });
}

// Async messages (notices, notifications, parameter descriptions) are ignored
void test_async()
{
    fixture fix;
    fix.req.add_prepare("SELECT 1", "mystmt");

    // Run the FSM
    BOOST_TEST_EQ(fix.fsm.resume(protocol::parse_complete{}), result_type::read);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::notice_response{}), result_type::read);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::notification_response{}), result_type::read);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::parameter_status{}), result_type::read);
    BOOST_TEST_EQ(fix.fsm.resume(protocol::ready_for_query{}), error_code());

    // Check handler messages
    fix.check({
        {response_msg_type::parse_complete, 0u}
    });
}

// TODO: recover this
// // If a request contains several syncs/queries, we read as many messages as these
// void test_impl_several_syncs()
// {
//     request req;
//     req.add_close_statement("abc");
//     req.add_simple_query("SELECT 1");
//     req.add_describe_statement("def");
//     std::vector<response_msg_type> msgs;
//     auto handler = [&msgs](const any_request_message& msg, diagnostics&) {
//         msgs.push_back(to_type(msg));
//         return response_handler_result::done();
//     };
//     diagnostics diag;

//     read_response_fsm_impl fsm{req, handler};

//     // Initiate
//     auto act = fsm.resume({});
//     BOOST_TEST_EQ(act, result_type::read);

//     // Server messages
//     act = fsm.resume(protocol::close_complete{});
//     BOOST_TEST_EQ(act, result_type::read);
//     act = fsm.resume(protocol::ready_for_query{});
//     BOOST_TEST_EQ(act, result_type::read);
//     act = fsm.resume(protocol::row_description{});
//     BOOST_TEST_EQ(act, result_type::read);
//     act = fsm.resume(protocol::data_row{});
//     BOOST_TEST_EQ(act, result_type::read);
//     act = fsm.resume(protocol::command_complete{});
//     BOOST_TEST_EQ(act, result_type::read);
//     act = fsm.resume(protocol::ready_for_query{});
//     BOOST_TEST_EQ(act, result_type::read);
//     act = fsm.resume(protocol::parameter_description{});
//     BOOST_TEST_EQ(act, result_type::read);
//     act = fsm.resume(protocol::ready_for_query{});
//     BOOST_TEST_EQ(act, error_code());

//     // Check handler messages
//     const response_msg_type expected_msgs[] = {
//         response_msg_type::close_complete,
//         response_msg_type::row_description,
//         response_msg_type::data_row,
//         response_msg_type::command_complete,
//         response_msg_type::parameter_description,
//     };
//     BOOST_TEST_ALL_EQ(msgs.begin(), msgs.end(), std::begin(expected_msgs), std::end(expected_msgs));
// }

// TODO: test errors
// TODO: test the public FSM API

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

    test_extended_query();
    test_async();
    // test_impl_several_syncs();

    return boost::report_errors();
}
