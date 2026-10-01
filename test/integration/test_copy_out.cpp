//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/capy/task.hpp>
#include <boost/core/lightweight_test.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

#include "nativepg/co_connection.hpp"
#include "nativepg/exec_state.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/request.hpp"
#include "nativepg/responses/command_info.hpp"
#include "nativepg/responses/copy_out_handler.hpp"
#include "nativepg/sqlstate_cond.hpp"
#include "test_utils/co_connection_utils.hpp"
#include "test_utils/corosio_utils.hpp"
#include "test_utils/printing.hpp"
#include "test_utils/test_cond_eq.hpp"
#include "test_utils/test_opt_eq.hpp"

namespace capy = boost::capy;
using namespace nativepg;
using namespace nativepg::test;
using namespace std::string_view_literals;

// Tests that COPY ... TO STDOUT is understood at the protocol level

namespace {

// A COPY OUT callback that appends to a string
auto collect_into(std::string& out)
{
    return [&out](std::span<const unsigned char> buff) {
        out.append(reinterpret_cast<const char*>(buff.data()), buff.size());
    };
}

// A COPY ... TO STDOUT is understood at the protocol level: the data reaches
// the handler and the command tag reports how many rows were copied
capy::task<> test_copy_out()
{
    // Setup
    auto conn = co_await establish_connection();

    request req_setup;
    req_setup.add_query("CREATE TEMPORARY TABLE copy_out_test (id INT, name TEXT)");
    req_setup.add_query("INSERT INTO copy_out_test VALUES (1, 'one'), (2, 'two'), (3, 'three')");

    if (!co_await checked_exec(conn, req_setup))
        co_return;

    // Copy the table out
    request req;
    req.add_query("COPY copy_out_test TO STDOUT");

    std::string data;
    command_info info;
    auto handler = copy_out_handler(collect_into(data), &info);

    exec_state st;
    if (!BOOST_TEST_EQ(conn.prepare_request(st, req, &handler), std::error_code()))
        co_return;
    if (!check_success(co_await conn.write_request(st)))
        co_return;
    while (!st.read_done())
    {
        if (!check_success(co_await conn.read_some_response(st)))
            co_return;
    }

    // The default COPY format is text: tab-separated columns, newline-separated rows
    check_success(st.handler_error());
    BOOST_TEST_EQ(data, "1\tone\n2\ttwo\n3\tthree\n"sv);
    test_opt_eq(info.affected_rows, std::uint64_t(3));

    co_await check_connection_usable(conn);
}

// A COPY that the server rejects reports the failure instead of starting a copy
capy::task<> test_copy_out_error()
{
    // Setup
    auto conn = co_await establish_connection();

    // The table doesn't exist, so the server answers with an error rather than
    // with a CopyOutResponse
    request req;
    req.add_query("COPY copy_out_does_not_exist TO STDOUT");

    std::string data;
    auto handler = copy_out_handler(collect_into(data));

    exec_state st;
    if (!BOOST_TEST_EQ(conn.prepare_request(st, req, &handler), std::error_code()))
        co_return;
    if (!check_success(co_await conn.write_request(st)))
        co_return;

    while (!st.read_done())
    {
        if (!check_success(co_await conn.read_some_response(st)))
            co_return;
    }

    // The failure is recorded in the state
    test_cond_eq(st.handler_error().code, sqlstate_cond::undefined_table);
    BOOST_TEST_NOT(st.handler_error().diag.message().empty());  // checking unreliable

    // No copy was ever started
    BOOST_TEST(data.empty());

    // A rejected COPY is not a protocol error
    co_await check_connection_usable(conn);
}

}  // namespace

int main()
{
    run_coroutine_test(test_copy_out());
    run_coroutine_test(test_copy_out_error());

    return boost::report_errors();
}
