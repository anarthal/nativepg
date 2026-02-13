//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/core/lightweight_test.hpp>
#include <boost/system/error_code.hpp>

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
#include "nativepg/response.hpp"
#include "nativepg/response_handler.hpp"
#include "printing.hpp"
#include "response_msg_type.hpp"
#include "test_utils.hpp"

using namespace nativepg;
using namespace nativepg::test;

namespace {

// Templating on num_msgs tests that we support heterogeneous types
template <std::size_t num_msgs>
struct mock_handler
{
    std::vector<on_msg_args> msgs;
    extended_error err;

    handler_setup_result setup(const request&, std::size_t offset) { return {offset + num_msgs}; }
    void on_message(const any_request_message& msg, std::size_t offset)
    {
        msgs.push_back({to_type(msg), offset});
    }
    const extended_error& result() const { return err; }
};

// Success case
void test_success_two_handlers()
{
    // Test setup
    request req;
    req.add_query("SELECT 1", {});
    response res{mock_handler<2>{}, mock_handler<3>{}};

    // Handler setup
    BOOST_TEST_EQ(res.setup(req, 0u), handler_setup_result(5u));

    // The 1st handler manages the first 2 request messages, the 2nd the other ones
    res.on_message(protocol::parse_complete{}, 0u);
    res.on_message(protocol::bind_complete{}, 1u);
    res.on_message(protocol::row_description{}, 2u);
    res.on_message(protocol::data_row{}, 3u);
    res.on_message(protocol::command_complete{}, 3u);

    // Result
    BOOST_TEST_EQ(res.result(), extended_error{});

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
    NATIVEPG_TEST_CONT_EQ(std::get<0>(res.handlers()).msgs, expected1);
    NATIVEPG_TEST_CONT_EQ(std::get<1>(res.handlers()).msgs, expected2);
}

// The 1st handler that returns an error is chosen as the overall error
void test_errors()
{
    // Setup
    response res{mock_handler<1>{}, mock_handler<1>{}, mock_handler<1>{}, mock_handler<1>{}};
    std::get<1>(res.handlers()).err = {client_errc::field_not_found, std::string("error")};
    std::get<2>(res.handlers()).err = {client_errc::incompatible_field_type, std::string("other")};

    const extended_error expected{client_errc::field_not_found, std::string("error")};
    BOOST_TEST_EQ(res.result(), expected);
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

void test_ignore_resultset()
{
    using h1 = errors_only_handler;
    using h2 = mock_handler<2>;

    response res{eo, h2{}};

    static_assert(std::is_same_v<decltype(res), response<h1, h2>>);
}

}  // namespace

int main()
{
    test_success_two_handlers();
    test_errors();
    test_deduction_guide();
    test_ignore_resultset();

    return boost::report_errors();
}