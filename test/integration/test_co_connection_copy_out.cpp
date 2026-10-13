//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/capy/task.hpp>
#include <boost/capy/test/write_sink.hpp>
#include <boost/core/lightweight_test.hpp>

#include <string_view>

#include "nativepg/co_connection.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/request.hpp"
#include "nativepg/sqlstate_cond.hpp"
#include "test_utils/co_connection_utils.hpp"
#include "test_utils/corosio_utils.hpp"
#include "test_utils/printing.hpp"
#include "test_utils/test_cond_eq.hpp"
#include "test_utils/test_string_contains.hpp"

// Covers co_connection::copy_out

namespace capy = boost::capy;
using namespace nativepg;
using namespace nativepg::test;
using namespace std::string_view_literals;

namespace {

constexpr auto create_table_sql = "CREATE TEMPORARY TABLE copy_out_test (id INT, name TEXT)"sv;

// A Copy-out producing data works
capy::task<> test_success()
{
    // Setup
    auto conn = co_await establish_connection();

    request req_setup;
    req_setup.add_query(create_table_sql);
    req_setup.add_query("INSERT INTO copy_out_test VALUES (1, 'one'), (2, 'two'), (3, 'three')");
    if (!co_await checked_exec(conn, req_setup))
        co_return;

    request req;
    req.add_query("COPY copy_out_test TO STDOUT");

    capy::test::write_sink sink;
    diagnostics diag;

    // Run the copy
    auto [ec, rows] = co_await conn.copy_out(req, &sink, &diag);
    if (!check_success(ec, diag))
        co_return;

    // Check
    BOOST_TEST_EQ(rows, 3u);
    BOOST_TEST_EQ(sink.data(), "1\tone\n2\ttwo\n3\tthree\n"sv);
    BOOST_TEST(sink.eof_called());

    co_await check_connection_usable(conn);
}

// A Copy-out producing no rows also works
capy::task<> test_success_no_data()
{
    // Setup
    auto conn = co_await establish_connection();

    if (!co_await checked_exec(conn, create_table_sql))
        co_return;

    request req;
    req.add_query("COPY copy_out_test TO STDOUT");

    capy::test::write_sink sink;
    diagnostics diag;

    // Run the copy
    auto [ec, rows] = co_await conn.copy_out(req, &sink, &diag);
    if (!check_success(ec, diag))
        co_return;

    // The copy succeeded, and copied nothing
    BOOST_TEST_EQ(rows, 0u);
    BOOST_TEST_EQ(sink.data(), std::string_view());
    BOOST_TEST(sink.eof_called());

    co_await check_connection_usable(conn);
}

// The server rejects the request, so it never enters copy-out mode
capy::task<> test_server_error()
{
    // Setup
    auto conn = co_await establish_connection();

    // The table doesn't exist
    request req;
    req.add_query("COPY copy_out_does_not_exist TO STDOUT");

    capy::test::write_sink sink;
    diagnostics diag;

    // Run the copy
    auto [ec, rows] = co_await conn.copy_out(req, &sink, &diag);

    // The server's error is reported
    test_cond_eq(ec, sqlstate_cond::undefined_table);
    test_string_contains(diag.message(), "copy_out_does_not_exist");

    // The copy never started, and a failed transfer doesn't finalize the sink
    BOOST_TEST_EQ(rows, 0u);
    BOOST_TEST_EQ(sink.data(), std::string_view());
    BOOST_TEST_NOT(sink.eof_called());

    // A rejected request is not a protocol error
    co_await check_connection_usable(conn);
}

}  // namespace

int main()
{
    run_coroutine_test(test_success());
    run_coroutine_test(test_success_no_data());
    run_coroutine_test(test_server_error());

    return boost::report_errors();
}
