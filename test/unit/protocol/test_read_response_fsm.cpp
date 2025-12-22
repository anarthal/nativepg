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

#include "nativepg/protocol/command_complete.hpp"
#include "nativepg/protocol/data_row.hpp"
#include "nativepg/protocol/describe.hpp"
#include "nativepg/protocol/read_response_fsm.hpp"
#include "nativepg/protocol/ready_for_query.hpp"
#include "nativepg/request.hpp"
#include "nativepg/response.hpp"

using namespace nativepg;
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

enum class response_msg_type
{
    bind_complete,
    close_complete,
    command_complete,
    data_row,
    parameter_description,
    row_description,
    no_data,
    empty_query_response,
    portal_suspended,
    error_response,
    parse_complete,
};

const char* to_string(response_msg_type value)
{
    switch (value)
    {
        case response_msg_type::bind_complete: return "bind_complete";
        case response_msg_type::close_complete: return "close_complete";
        case response_msg_type::command_complete: return "command_complete";
        case response_msg_type::data_row: return "data_row";
        case response_msg_type::parameter_description: return "parameter_description";
        case response_msg_type::row_description: return "row_description";
        case response_msg_type::no_data: return "no_data";
        case response_msg_type::empty_query_response: return "empty_query_response";
        case response_msg_type::portal_suspended: return "portal_suspended";
        case response_msg_type::error_response: return "error_response";
        case response_msg_type::parse_complete: return "parse_complete";
        default: return "<unknown response_msg_type>";
    }
}

std::ostream& operator<<(std::ostream& os, response_msg_type t) { return os << to_string(t); }

response_msg_type to_type(const any_request_message& msg)
{
    struct visitor
    {
        // clang-format off
        response_msg_type operator()(const protocol::bind_complete&) const { return response_msg_type::bind_complete;}
        response_msg_type operator()(const protocol::close_complete&) const { return response_msg_type::close_complete;}
        response_msg_type operator()(const protocol::command_complete&) const { return response_msg_type::command_complete;}
        response_msg_type operator()(const protocol::data_row&) const { return response_msg_type::data_row;}
        response_msg_type operator()(const protocol::parameter_description&) const { return response_msg_type::parameter_description;}
        response_msg_type operator()(const protocol::row_description&) const { return response_msg_type::row_description;}
        response_msg_type operator()(const protocol::no_data&) const { return response_msg_type::no_data;}
        response_msg_type operator()(const protocol::empty_query_response&) const { return response_msg_type::empty_query_response;}
        response_msg_type operator()(const protocol::portal_suspended&) const { return response_msg_type::portal_suspended;}
        response_msg_type operator()(const protocol::error_response&) const { return response_msg_type::error_response;}
        response_msg_type operator()(const protocol::parse_complete&) const { return response_msg_type::parse_complete;}
        // clang-format on
    };

    return boost::variant2::visit(visitor{}, msg);
}

// Response to a simple query
void test_impl_query()
{
    request req;
    req.add_simple_query("SELECT 1");
    std::vector<response_msg_type> msgs;

    read_response_fsm_impl fsm{req, [&msgs](const any_request_message& msg) {
                                   msgs.push_back(to_type(msg));
                                   return error_code();
                               }};

    // Initiate
    auto act = fsm.resume({});
    BOOST_TEST_EQ(act, read_response_fsm_impl::result_type::read);

    // Server messages
    act = fsm.resume(protocol::row_description{});
    BOOST_TEST_EQ(act, read_response_fsm_impl::result_type::read);
    act = fsm.resume(protocol::data_row{});
    BOOST_TEST_EQ(act, read_response_fsm_impl::result_type::read);
    act = fsm.resume(protocol::data_row{});
    BOOST_TEST_EQ(act, read_response_fsm_impl::result_type::read);
    act = fsm.resume(protocol::command_complete{});
    BOOST_TEST_EQ(act, read_response_fsm_impl::result_type::read);
    act = fsm.resume(protocol::ready_for_query{});
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

}  // namespace

int main()
{
    test_impl_query();

    return boost::report_errors();
}
