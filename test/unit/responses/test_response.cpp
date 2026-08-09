//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/core/lightweight_test.hpp>

#include <cstddef>
#include <vector>

#include "nativepg/client_errc.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/protocol/bind.hpp"
#include "nativepg/protocol/command_complete.hpp"
#include "nativepg/protocol/data_row.hpp"
#include "nativepg/protocol/describe.hpp"
#include "nativepg/protocol/parse.hpp"
#include "nativepg/request.hpp"
#include "nativepg/responses/response.hpp"
#include "nativepg/responses/response_handler.hpp"
#include "test_utils/printing.hpp"
#include "test_utils/response_msg_type.hpp"
#include "test_utils/test_range_eq.hpp"

using namespace nativepg;
using namespace nativepg::test;

namespace {

// Templating on num_msgs tests that we support heterogeneous types
template <std::size_t num_msgs>
struct mock_handler
{
    std::vector<on_msg_args> msgs;
    extended_error err_to_return;  // will be reported by on_message

    handler_setup_result setup(const request&, std::size_t offset) { return {offset + num_msgs}; }
    void on_message(const any_request_message& msg, std::size_t offset, extended_error& err)
    {
        msgs.push_back({to_type(msg), offset});
        err = err_to_return;
    }
};

// Calls on_message and returns the produced error
// TODO: duplicated
template <response_handler Handler>
extended_error feed(Handler& h, const any_request_message& msg, std::size_t offset)
{
    extended_error err;
    h.on_message(msg, offset, err);
    return err;
}

void test_two_handlers()
{
    // Test setup
    request req;
    req.add_query("SELECT 1", {});
    response res{mock_handler<2>{}, mock_handler<3>{}};
    extended_error err;

    // Handler setup
    BOOST_TEST_EQ(res.setup(req, 0u), handler_setup_result(5u));

    // The 1st handler manages the first 2 request messages, the 2nd the other ones
    BOOST_TEST_EQ(feed(res, protocol::parse_complete{}, 0u), extended_error{});
    BOOST_TEST_EQ(feed(res, protocol::bind_complete{}, 1u), extended_error{});
    BOOST_TEST_EQ(feed(res, protocol::row_description{}, 2u), extended_error{});
    BOOST_TEST_EQ(feed(res, protocol::data_row{}, 3u), extended_error{});
    BOOST_TEST_EQ(feed(res, protocol::command_complete{}, 3u), extended_error{});

    // Result
    BOOST_TEST_EQ(err, extended_error{});

    // Check messages
    const on_msg_args expected1[] = {
        {response_msg_type::parse_complete, 0u},
        {response_msg_type::bind_complete,  1u},
    };
    const on_msg_args expected2[] = {
        {response_msg_type::row_description,  2u},
        {response_msg_type::data_row,         3u},
        {response_msg_type::command_complete, 3u},
    };
    test_range_eq(std::get<0>(res.handlers()).msgs, expected1);
    test_range_eq(std::get<1>(res.handlers()).msgs, expected2);
}

// Errors are forwarded to the inner handler
void test_errors()
{
    // Setup
    const extended_error first_error{client_errc::field_not_found, std::string("error")};
    const extended_error second_error{client_errc::incompatible_field_type, std::string("other")};
    request req;
    response res{mock_handler<1>{}, mock_handler<1>{}, mock_handler<1>{}, mock_handler<1>{}};
    std::get<1>(res.handlers()).err_to_return = first_error;
    std::get<2>(res.handlers()).err_to_return = second_error;
    BOOST_TEST_EQ(res.setup(req, 0u), 4u);

    // Call the handler
    BOOST_TEST_EQ(feed(res, protocol::parse_complete{}, 0u), extended_error{});
    BOOST_TEST_EQ(feed(res, protocol::parse_complete{}, 1u), first_error);
    BOOST_TEST_EQ(feed(res, protocol::parse_complete{}, 2u), second_error);
    BOOST_TEST_EQ(feed(res, protocol::parse_complete{}, 3u), extended_error{});
}

// Response can be copied
void test_copy()
{
    response res{mock_handler<1>{}, mock_handler<2>{}};
    auto res2 = res;
    res2 = res;
}

// Response can be moved
void test_move()
{
    response res{mock_handler<1>{}, mock_handler<2>{}};
    response res2{mock_handler<1>{}, mock_handler<2>{}};
    response<mock_handler<1>, mock_handler<2>> res3{std::move(res)};
    res3 = std::move(res2);
}

// The deduction guide works correctly
void test_deduction_guide()
{
    using h1 = mock_handler<1>;
    using h2 = mock_handler<2>;

    h1 h1_lvalue;
    const h1 h1_const;

    response res{h1_lvalue, h1_const, h1{}, h2{}};

    static_assert(std::is_same_v<decltype(res), response<h1, h1, h1, h2>>);
}

}  // namespace

int main()
{
    test_two_handlers();
    test_errors();
    test_deduction_guide();
    test_copy();
    test_move();

    return boost::report_errors();
}