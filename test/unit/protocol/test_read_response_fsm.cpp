//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/core/lightweight_test.hpp>
#include <boost/system/detail/error_code.hpp>
#include <boost/system/error_code.hpp>
#include <boost/variant2/variant.hpp>

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
#include "nativepg/response.hpp"
#include "response_msg_type.hpp"

using namespace nativepg;
using namespace nativepg::test;
using boost::system::error_code;
using protocol::detail::read_response_fsm_impl;

// Operators
static const char* to_string(read_response_fsm_impl::result_type t)
{
    switch (t)
    {
        case read_response_fsm_impl::result_type::done: return "done";
        case read_response_fsm_impl::result_type::read: return "read";
        default: return "<unknown read_response_fsm_impl::result_type>";
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
    if (value.type == read_response_fsm_impl::result_type::done)
        os << ", .ec=" << value.ec;
    return os << " }";
}

}  // namespace nativepg::protocol::detail

namespace {

// Response to a simple query
void test_impl_simple_query()
{
    request req;
    req.add_simple_query("SELECT 1");
    std::vector<response_msg_type> msgs;
    auto handler = [&msgs](const any_request_message& msg, diagnostics&) {
        msgs.push_back(to_type(msg));
        return error_code();
    };
    diagnostics diag;

    read_response_fsm_impl fsm{req, handler};

    // Initiate
    auto act = fsm.resume(diag, {});
    BOOST_TEST_EQ(act, read_response_fsm_impl::result_type::read);

    // Server messages
    act = fsm.resume(diag, protocol::row_description{});
    BOOST_TEST_EQ(act, read_response_fsm_impl::result_type::read);
    act = fsm.resume(diag, protocol::data_row{});
    BOOST_TEST_EQ(act, read_response_fsm_impl::result_type::read);
    act = fsm.resume(diag, protocol::data_row{});
    BOOST_TEST_EQ(act, read_response_fsm_impl::result_type::read);
    act = fsm.resume(diag, protocol::command_complete{});
    BOOST_TEST_EQ(act, read_response_fsm_impl::result_type::read);
    act = fsm.resume(diag, protocol::ready_for_query{});
    BOOST_TEST_EQ(act, error_code());

    // Check handler messages
    const response_msg_type expected_msgs[] = {
        response_msg_type::row_description,
        response_msg_type::data_row,
        response_msg_type::data_row,
        response_msg_type::command_complete,
    };
    BOOST_TEST_ALL_EQ(msgs.begin(), msgs.end(), std::begin(expected_msgs), std::end(expected_msgs));
}

// Response to an extended query
void test_impl_extended_query()
{
    request req;
    req.add_query("SELECT 1", {});
    std::vector<response_msg_type> msgs;
    auto handler = [&msgs](const any_request_message& msg, diagnostics&) {
        msgs.push_back(to_type(msg));
        return error_code();
    };
    diagnostics diag;

    read_response_fsm_impl fsm{req, handler};

    // Initiate
    auto act = fsm.resume(diag, {});
    BOOST_TEST_EQ(act, read_response_fsm_impl::result_type::read);

    // Server messages
    act = fsm.resume(diag, protocol::bind_complete{});
    BOOST_TEST_EQ(act, read_response_fsm_impl::result_type::read);
    act = fsm.resume(diag, protocol::parse_complete{});
    BOOST_TEST_EQ(act, read_response_fsm_impl::result_type::read);
    act = fsm.resume(diag, protocol::row_description{});
    BOOST_TEST_EQ(act, read_response_fsm_impl::result_type::read);
    act = fsm.resume(diag, protocol::data_row{});
    BOOST_TEST_EQ(act, read_response_fsm_impl::result_type::read);
    act = fsm.resume(diag, protocol::portal_suspended{});
    BOOST_TEST_EQ(act, read_response_fsm_impl::result_type::read);
    act = fsm.resume(diag, protocol::ready_for_query{});
    BOOST_TEST_EQ(act, error_code());

    // Check handler messages
    const response_msg_type expected_msgs[] = {
        response_msg_type::bind_complete,
        response_msg_type::parse_complete,
        response_msg_type::row_description,
        response_msg_type::data_row,
        response_msg_type::portal_suspended,
    };
    BOOST_TEST_ALL_EQ(msgs.begin(), msgs.end(), std::begin(expected_msgs), std::end(expected_msgs));
}

// All messages to be forwarded to the handler are
void test_impl_all_msg_types()
{
    request req;
    req.add(protocol::sync{});

    std::vector<response_msg_type> msgs;
    auto handler = [&msgs](const any_request_message& msg, diagnostics&) {
        msgs.push_back(to_type(msg));
        return error_code();
    };

    diagnostics diag;

    read_response_fsm_impl fsm{req, handler};

    // Initiate
    auto act = fsm.resume(diag, {});
    BOOST_TEST_EQ(act, read_response_fsm_impl::result_type::read);

    // Server messages
    act = fsm.resume(diag, protocol::bind_complete{});
    BOOST_TEST_EQ(act, read_response_fsm_impl::result_type::read);
    act = fsm.resume(diag, protocol::close_complete{});
    BOOST_TEST_EQ(act, read_response_fsm_impl::result_type::read);
    act = fsm.resume(diag, protocol::command_complete{});
    BOOST_TEST_EQ(act, read_response_fsm_impl::result_type::read);
    act = fsm.resume(diag, protocol::data_row{});
    BOOST_TEST_EQ(act, read_response_fsm_impl::result_type::read);
    act = fsm.resume(diag, protocol::parameter_description{});
    BOOST_TEST_EQ(act, read_response_fsm_impl::result_type::read);
    act = fsm.resume(diag, protocol::row_description{});
    BOOST_TEST_EQ(act, read_response_fsm_impl::result_type::read);
    act = fsm.resume(diag, protocol::no_data{});
    BOOST_TEST_EQ(act, read_response_fsm_impl::result_type::read);
    act = fsm.resume(diag, protocol::empty_query_response{});
    BOOST_TEST_EQ(act, read_response_fsm_impl::result_type::read);
    act = fsm.resume(diag, protocol::portal_suspended{});
    BOOST_TEST_EQ(act, read_response_fsm_impl::result_type::read);
    act = fsm.resume(diag, protocol::error_response{});
    BOOST_TEST_EQ(act, read_response_fsm_impl::result_type::read);
    act = fsm.resume(diag, protocol::parse_complete{});
    BOOST_TEST_EQ(act, read_response_fsm_impl::result_type::read);
    act = fsm.resume(diag, protocol::ready_for_query{});
    BOOST_TEST_EQ(act, error_code());

    // Check handler messages
    const response_msg_type expected_msgs[] = {
        response_msg_type::bind_complete,
        response_msg_type::close_complete,
        response_msg_type::command_complete,
        response_msg_type::data_row,
        response_msg_type::parameter_description,
        response_msg_type::row_description,
        response_msg_type::no_data,
        response_msg_type::empty_query_response,
        response_msg_type::portal_suspended,
        response_msg_type::error_response,
        response_msg_type::parse_complete,
    };
    BOOST_TEST_ALL_EQ(msgs.begin(), msgs.end(), std::begin(expected_msgs), std::end(expected_msgs));
}

// Async messages (notices, notifications, parameter descriptions) are ignored
void test_impl_async()
{
    request req;
    req.add(protocol::sync{});

    std::vector<response_msg_type> msgs;
    auto handler = [&msgs](const any_request_message& msg, diagnostics&) {
        msgs.push_back(to_type(msg));
        return error_code();
    };

    diagnostics diag;

    read_response_fsm_impl fsm{req, handler};

    // Initiate
    auto act = fsm.resume(diag, {});
    BOOST_TEST_EQ(act, read_response_fsm_impl::result_type::read);

    // Server messages
    act = fsm.resume(diag, protocol::bind_complete{});
    BOOST_TEST_EQ(act, read_response_fsm_impl::result_type::read);
    act = fsm.resume(diag, protocol::notice_response{});
    BOOST_TEST_EQ(act, read_response_fsm_impl::result_type::read);
    act = fsm.resume(diag, protocol::notification_response{});
    BOOST_TEST_EQ(act, read_response_fsm_impl::result_type::read);
    act = fsm.resume(diag, protocol::parameter_status{});
    BOOST_TEST_EQ(act, read_response_fsm_impl::result_type::read);
    act = fsm.resume(diag, protocol::ready_for_query{});
    BOOST_TEST_EQ(act, error_code());

    // Check handler messages
    const response_msg_type expected_msgs[] = {
        response_msg_type::bind_complete,
    };
    BOOST_TEST_ALL_EQ(msgs.begin(), msgs.end(), std::begin(expected_msgs), std::end(expected_msgs));
}

// If a request contains several syncs/queries, we read as many messages as these
void test_impl_several_syncs()
{
    request req;
    req.add_close_statement("abc");
    req.add_simple_query("SELECT 1");
    req.add_describe_statement("def");
    std::vector<response_msg_type> msgs;
    auto handler = [&msgs](const any_request_message& msg, diagnostics&) {
        msgs.push_back(to_type(msg));
        return error_code();
    };
    diagnostics diag;

    read_response_fsm_impl fsm{req, handler};

    // Initiate
    auto act = fsm.resume(diag, {});
    BOOST_TEST_EQ(act, read_response_fsm_impl::result_type::read);

    // Server messages
    act = fsm.resume(diag, protocol::close_complete{});
    BOOST_TEST_EQ(act, read_response_fsm_impl::result_type::read);
    act = fsm.resume(diag, protocol::ready_for_query{});
    BOOST_TEST_EQ(act, read_response_fsm_impl::result_type::read);
    act = fsm.resume(diag, protocol::row_description{});
    BOOST_TEST_EQ(act, read_response_fsm_impl::result_type::read);
    act = fsm.resume(diag, protocol::data_row{});
    BOOST_TEST_EQ(act, read_response_fsm_impl::result_type::read);
    act = fsm.resume(diag, protocol::command_complete{});
    BOOST_TEST_EQ(act, read_response_fsm_impl::result_type::read);
    act = fsm.resume(diag, protocol::ready_for_query{});
    BOOST_TEST_EQ(act, read_response_fsm_impl::result_type::read);
    act = fsm.resume(diag, protocol::parameter_description{});
    BOOST_TEST_EQ(act, read_response_fsm_impl::result_type::read);
    act = fsm.resume(diag, protocol::ready_for_query{});
    BOOST_TEST_EQ(act, error_code());

    // Check handler messages
    const response_msg_type expected_msgs[] = {
        response_msg_type::close_complete,
        response_msg_type::row_description,
        response_msg_type::data_row,
        response_msg_type::command_complete,
        response_msg_type::parameter_description,
    };
    BOOST_TEST_ALL_EQ(msgs.begin(), msgs.end(), std::begin(expected_msgs), std::end(expected_msgs));
}

// A handler may return needs_more to signal it wants more messages.
// No error occurs unless we reach the end of the pipeline
void test_impl_needs_more_success()
{
    request req;
    req.add(protocol::sync{});
    std::vector<response_msg_type> msgs;
    auto handler = [&msgs](const any_request_message& msg, diagnostics&) {
        msgs.push_back(to_type(msg));
        return msgs.size() >= 2u ? error_code() : error_code(client_errc::needs_more);
    };
    diagnostics diag;

    read_response_fsm_impl fsm{req, handler};

    // Initiate
    auto act = fsm.resume(diag, {});
    BOOST_TEST_EQ(act, read_response_fsm_impl::result_type::read);

    // Server messages
    act = fsm.resume(diag, protocol::no_data{});
    BOOST_TEST_EQ(act, read_response_fsm_impl::result_type::read);
    act = fsm.resume(diag, protocol::command_complete{});
    BOOST_TEST_EQ(act, read_response_fsm_impl::result_type::read);
    act = fsm.resume(diag, protocol::ready_for_query{});
    BOOST_TEST_EQ(act, error_code());

    // Check handler messages
    const response_msg_type expected_msgs[] = {
        response_msg_type::no_data,
        response_msg_type::command_complete,
    };
    BOOST_TEST_ALL_EQ(msgs.begin(), msgs.end(), std::begin(expected_msgs), std::end(expected_msgs));
}

// If we reach the end of the pipeline and the handler still needs more, this is an error
void test_impl_needs_more_error()
{
    request req;
    req.add(protocol::sync{});
    std::vector<response_msg_type> msgs;
    auto handler = [&msgs](const any_request_message& msg, diagnostics&) {
        msgs.push_back(to_type(msg));
        return error_code(client_errc::needs_more);
    };
    diagnostics diag;

    read_response_fsm_impl fsm{req, handler};

    // Initiate
    auto act = fsm.resume(diag, {});
    BOOST_TEST_EQ(act, read_response_fsm_impl::result_type::read);

    // Server messages
    act = fsm.resume(diag, protocol::no_data{});
    BOOST_TEST_EQ(act, read_response_fsm_impl::result_type::read);
    act = fsm.resume(diag, protocol::command_complete{});
    BOOST_TEST_EQ(act, read_response_fsm_impl::result_type::read);
    act = fsm.resume(diag, protocol::ready_for_query{});
    BOOST_TEST_EQ(act, error_code(client_errc::needs_more));
}

// TODO: test errors
// TODO: test the public FSM API

}  // namespace

int main()
{
    test_impl_simple_query();
    test_impl_extended_query();
    test_impl_all_msg_types();
    test_impl_async();
    test_impl_several_syncs();

    test_impl_needs_more_success();
    test_impl_needs_more_error();

    return boost::report_errors();
}
