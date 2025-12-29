//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/core/lightweight_test.hpp>
#include <boost/system/error_code.hpp>

#include <ostream>
#include <vector>

#include "nativepg/client_errc.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/protocol/data_row.hpp"
#include "nativepg/protocol/describe.hpp"
#include "nativepg/response.hpp"
#include "nativepg/response_handler.hpp"
#include "response_msg_type.hpp"

using namespace nativepg;
using namespace nativepg::test;

// Printing
namespace nativepg {

std::ostream& operator<<(std::ostream& os, response_handler_result r)
{
    return os << "{ .is_done=" << r.is_done() << ", .error=" << r.error() << " }";
}

}  // namespace nativepg

namespace {

void test_success_two_handlers()
{
    // Setup
    std::vector<response_msg_type> msgs1, msgs2;

    auto h1 = [&msgs1](const any_request_message& msg, diagnostics&) {
        msgs1.push_back(to_type(msg));
        return response_handler_result({}, msgs1.size() >= 2u);
    };

    auto h2 = [&msgs2](const any_request_message& msg, diagnostics&) {
        msgs2.push_back(to_type(msg));
        return response_handler_result::done({});
    };

    response res{h1, h2};
    diagnostics diag;

    // The 1st handler needs 2 messages, the 3rd one just one
    auto ec = res(protocol::row_description{}, diag);
    BOOST_TEST_EQ(ec, response_handler_result::needs_more());
    ec = res(protocol::data_row{}, diag);
    BOOST_TEST_EQ(ec, response_handler_result::needs_more());
    ec = res(protocol::row_description{}, diag);
    BOOST_TEST_EQ(ec, response_handler_result::done({}));  // done

    // Passing another message is an error
    ec = res(protocol::data_row{}, diag);
    BOOST_TEST_EQ(ec, response_handler_result::done(client_errc::incompatible_response_length));

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
        response_handler_result operator()(const any_request_message&, diagnostics&) { return {}; }
    };
    struct h2_t
    {
        response_handler_result operator()(const any_request_message&, diagnostics&) { return {}; }
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