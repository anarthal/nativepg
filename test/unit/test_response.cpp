//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/core/lightweight_test.hpp>
#include <boost/system/error_code.hpp>

#include <vector>

#include "nativepg/client_errc.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/protocol/data_row.hpp"
#include "nativepg/protocol/describe.hpp"
#include "nativepg/response.hpp"

using namespace nativepg;
using boost::system::error_code;

namespace {

// TODO: duplicated
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

void test_success_two_handlers()
{
    // Setup
    std::vector<response_msg_type> msgs1, msgs2;

    auto h1 = [&msgs1](const any_request_message& msg, diagnostics&) {
        msgs1.push_back(to_type(msg));
        return msgs1.size() >= 2u ? error_code() : client_errc::needs_more;
    };

    auto h2 = [&msgs2](const any_request_message& msg, diagnostics&) {
        msgs2.push_back(to_type(msg));
        return error_code();
    };

    response res{h1, h2};
    diagnostics diag;

    // The 1st handler needs 2 messages, the 3rd one just one
    auto ec = res(protocol::row_description{}, diag);
    BOOST_TEST_EQ(ec, error_code(client_errc::needs_more));
    ec = res(protocol::data_row{}, diag);
    BOOST_TEST_EQ(ec, error_code(client_errc::needs_more));
    ec = res(protocol::row_description{}, diag);
    BOOST_TEST_EQ(ec, error_code());  // done

    // Passing another message is an error
    ec = res(protocol::data_row{}, diag);
    BOOST_TEST_EQ(ec, error_code(client_errc::unexpected_message));

    // Check messages
    std::array expected1{response_msg_type::row_description, response_msg_type::data_row};
    std::array expected2{response_msg_type::row_description};
    BOOST_TEST_ALL_EQ(msgs1.begin(), msgs1.end(), expected1.begin(), expected1.end());
    BOOST_TEST_ALL_EQ(msgs2.begin(), msgs2.end(), expected2.begin(), expected2.end());
}

// The deduction guide works correctly
void test_deduction_guide()
{
    struct h1_t
    {
        error_code operator()(const any_request_message&, diagnostics&) { return {}; }
    };
    struct h2_t
    {
        error_code operator()(const any_request_message&, diagnostics&) { return {}; }
    };

    h1_t h1_lvalue;
    const h1_t h1_const;

    response res{h1_lvalue, h1_const, h1_t{}, h2_t{}};

    static_assert(std::is_same_v<decltype(res), response<h1_t, h1_t, h1_t, h2_t>>);
}

}  // namespace

int main()
{
    test_success_two_handlers();
    test_deduction_guide();

    return boost::report_errors();
}