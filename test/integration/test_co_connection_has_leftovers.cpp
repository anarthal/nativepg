//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/capy/ex/this_coro.hpp>
#include <boost/capy/results.hpp>
#include <boost/capy/task.hpp>
#include <boost/core/lightweight_test.hpp>

#include <system_error>

#include "nativepg/co_connection.hpp"
#include "nativepg/exec_state.hpp"
#include "nativepg/request.hpp"
#include "nativepg/responses/check.hpp"
#include "nativepg/responses/response_handler.hpp"
#include "test_utils/co_connection_utils.hpp"
#include "test_utils/corosio_utils.hpp"

// Covers co_connection::has_leftovers

namespace capy = boost::capy;
using namespace nativepg;
using namespace nativepg::test;

namespace {

// A connection that was never established has nothing to clean up
capy::task<> test_never_connected()
{
    co_connection conn{co_await capy::this_coro::executor};

    BOOST_TEST_NOT(conn.has_leftovers());
}

// After a fatal error, has_leftovers() returns false
// (no recovery possible)
capy::task<> test_fatal_error()
{
    // Setup
    auto conn = co_await establish_connection();

    // Ask the server to kill the backend serving us. It answers with a FATAL
    // error and closes the connection, so our reader never sees a ReadyForQuery.
    // There is a follow-up query that will receive no response
    request req;
    req.add_query("SELECT pg_terminate_backend(pg_backend_pid())");
    req.add_query("SELECT pg_sleep(999)");

    // Executing it fails
    auto [ec] = co_await conn.exec(req, check());
    BOOST_TEST_NE(ec, std::error_code());

    // Nothing is reported once the connection is gone
    BOOST_TEST_NOT(conn.is_connected());
    BOOST_TEST_NOT(conn.has_leftovers());
}

// A state that has been written but not read keeps the connection busy
capy::task<> test_exec_state_linked()
{
    // Setup
    auto conn = co_await establish_connection();

    request req;
    req.add_query("SELECT $1 AS value", 42);
    check handler;

    // Preparing a state doesn't attach it to the connection
    exec_state st;
    if (!BOOST_TEST_EQ(conn.prepare_request(st, req, &handler), std::error_code()))
        co_return;
    BOOST_TEST_NOT(conn.has_leftovers());

    // Writing it does
    if (!check_success(co_await conn.write_request(st)))
        co_return;
    BOOST_TEST(conn.has_leftovers());

    // Reading the response in full settles the debt
    while (!st.read_done())
    {
        if (!check_success(co_await conn.read_some_response(st)))
            co_return;
    }
    BOOST_TEST_NOT(conn.has_leftovers());

    co_await check_connection_usable(conn);
}

// If a the response hasn't been fully read, that's a leftover
capy::task<> test_exec_state_pending_read()
{
    // Setup
    auto conn = co_await establish_connection();

    request req;
    req.add_query("SELECT $1 AS value", 42);
    check handler;

    // Leave the response unread
    exec_state st;
    if (!BOOST_TEST_EQ(conn.prepare_request(st, req, &handler), std::error_code()))
        co_return;
    if (!check_success(co_await conn.write_request(st)))
        co_return;
    st.reset();

    // The response hasn't been read yet
    BOOST_TEST(conn.has_leftovers());

    // The next request absorbs it
    if (!co_await checked_exec(conn, "SELECT 1"))
        co_return;
    BOOST_TEST_NOT(conn.has_leftovers());

    co_await check_connection_usable(conn);
}

// An established connection with nothing outstanding has no leftovers
capy::task<> test_idle_connection()
{
    // Setup
    auto conn = co_await establish_connection();
    BOOST_TEST_NOT(conn.has_leftovers());

    // An operation that ran to completion leaves nothing behind
    if (!co_await checked_exec(conn, "SELECT 42"))
        co_return;
    BOOST_TEST_NOT(conn.has_leftovers());
}

}  // namespace

int main()
{
    run_coroutine_test(test_never_connected());
    run_coroutine_test(test_fatal_error());
    run_coroutine_test(test_exec_state_linked());
    run_coroutine_test(test_exec_state_pending_read());
    run_coroutine_test(test_idle_connection());

    return boost::report_errors();
}
