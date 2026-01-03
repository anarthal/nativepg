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

    void check(
        std::initializer_list<on_msg_args> expected,
        boost::source_location loc = BOOST_CURRENT_LOCATION
    )
    {
        if (!BOOST_TEST_ALL_EQ(msgs.begin(), msgs.end(), expected.begin(), expected.end()))
            std::cerr << "Called from " << loc << std::endl;
    }
};

// --- Responses to a simple query ---
void test_impl_simple_query()
{
    request req;
    req.add_simple_query("SELECT 1");
    mock_handler handler;
    read_response_fsm_impl fsm{req, handler};

    // Run the FSM
    BOOST_TEST_EQ(fsm.resume(protocol::row_description{}), result_type::read);
    BOOST_TEST_EQ(fsm.resume(protocol::data_row{}), result_type::read);
    BOOST_TEST_EQ(fsm.resume(protocol::data_row{}), result_type::read);
    BOOST_TEST_EQ(fsm.resume(protocol::command_complete{}), result_type::read);
    BOOST_TEST_EQ(fsm.resume(protocol::ready_for_query{}), error_code());

    // Check handler messages
    handler.check({
        {response_msg_type::row_description,  0u},
        {response_msg_type::data_row,         0u},
        {response_msg_type::data_row,         0u},
        {response_msg_type::command_complete, 0u},
    });
}

// Response to an extended query
void test_impl_extended_query()
{
    request req;
    req.add_query("SELECT 1", {});
    mock_handler handler;
    read_response_fsm_impl fsm{req, handler};

    // Run the FSM
    BOOST_TEST_EQ(fsm.resume(protocol::parse_complete{}), result_type::read);
    BOOST_TEST_EQ(fsm.resume(protocol::bind_complete{}), result_type::read);
    BOOST_TEST_EQ(fsm.resume(protocol::row_description{}), result_type::read);
    BOOST_TEST_EQ(fsm.resume(protocol::data_row{}), result_type::read);
    BOOST_TEST_EQ(fsm.resume(protocol::command_complete{}), result_type::read);
    BOOST_TEST_EQ(fsm.resume(protocol::ready_for_query{}), error_code());

    // Check handler messages
    handler.check({
        {response_msg_type::parse_complete,   0u},
        {response_msg_type::bind_complete,    1u},
        {response_msg_type::row_description,  2u},
        {response_msg_type::data_row,         3u},
        {response_msg_type::command_complete, 3u},
    });
}

// Response to an individual parse
void test_impl_parse()
{
    request req;
    req.add_prepare("SELECT 1", "mystmt");
    mock_handler handler;
    read_response_fsm_impl fsm{req, handler};

    // Ru the FSM
    BOOST_TEST_EQ(fsm.resume(protocol::parse_complete{}), result_type::read);
    BOOST_TEST_EQ(fsm.resume(protocol::ready_for_query{}), error_code());

    // Check handler messages
    handler.check({
        {response_msg_type::parse_complete, 0u},
    });
}

// Async messages (notices, notifications, parameter descriptions) are ignored
void test_impl_async()
{
    request req;
    req.add_prepare("SELECT 1", "mystmt");
    mock_handler handler;
    read_response_fsm_impl fsm{req, handler};

    // Run the FSM
    BOOST_TEST_EQ(fsm.resume(protocol::parse_complete{}), result_type::read);
    BOOST_TEST_EQ(fsm.resume(protocol::notice_response{}), result_type::read);
    BOOST_TEST_EQ(fsm.resume(protocol::notification_response{}), result_type::read);
    BOOST_TEST_EQ(fsm.resume(protocol::parameter_status{}), result_type::read);
    BOOST_TEST_EQ(fsm.resume(protocol::ready_for_query{}), error_code());

    // Check handler messages
    handler.check({
        {response_msg_type::parse_complete, 0u}
    });
}

// TODO: recover this
// // All messages to be forwarded to the handler are
// void test_impl_all_msg_types()
// {
//     request req;
//     req.add(protocol::sync{});

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
//     act = fsm.resume(protocol::bind_complete{});
//     BOOST_TEST_EQ(act, result_type::read);
//     act = fsm.resume(protocol::close_complete{});
//     BOOST_TEST_EQ(act, result_type::read);
//     act = fsm.resume(protocol::command_complete{});
//     BOOST_TEST_EQ(act, result_type::read);
//     act = fsm.resume(protocol::data_row{});
//     BOOST_TEST_EQ(act, result_type::read);
//     act = fsm.resume(protocol::parameter_description{});
//     BOOST_TEST_EQ(act, result_type::read);
//     act = fsm.resume(protocol::row_description{});
//     BOOST_TEST_EQ(act, result_type::read);
//     act = fsm.resume(protocol::no_data{});
//     BOOST_TEST_EQ(act, result_type::read);
//     act = fsm.resume(protocol::empty_query_response{});
//     BOOST_TEST_EQ(act, result_type::read);
//     act = fsm.resume(protocol::portal_suspended{});
//     BOOST_TEST_EQ(act, result_type::read);
//     act = fsm.resume(protocol::error_response{});
//     BOOST_TEST_EQ(act, result_type::read);
//     act = fsm.resume(protocol::parse_complete{});
//     BOOST_TEST_EQ(act, result_type::read);
//     act = fsm.resume(protocol::ready_for_query{});
//     BOOST_TEST_EQ(act, error_code());

//     // Check handler messages
//     const response_msg_type expected_msgs[] = {
//         response_msg_type::bind_complete,
//         response_msg_type::close_complete,
//         response_msg_type::command_complete,
//         response_msg_type::data_row,
//         response_msg_type::parameter_description,
//         response_msg_type::row_description,
//         response_msg_type::no_data,
//         response_msg_type::empty_query_response,
//         response_msg_type::portal_suspended,
//         response_msg_type::error_response,
//         response_msg_type::parse_complete,
//     };
//     BOOST_TEST_ALL_EQ(msgs.begin(), msgs.end(), std::begin(expected_msgs), std::end(expected_msgs));
// }

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
    test_impl_simple_query();
    test_impl_extended_query();
    test_impl_parse();
    // test_impl_all_msg_types();
    test_impl_async();
    // test_impl_several_syncs();

    return boost::report_errors();
}
