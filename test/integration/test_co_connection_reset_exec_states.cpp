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
#include "nativepg/write_status.hpp"
#include "test_utils/co_connection_utils.hpp"
#include "test_utils/corosio_utils.hpp"
#include "test_utils/exec_state_utils.hpp"
#include "test_utils/printing.hpp"

namespace capy = boost::capy;
using namespace nativepg;
using namespace nativepg::test;

namespace {

// A connection that was never connected has nothing attached
capy::task<> test_never_connected()
{
    co_connection conn{co_await capy::this_coro::executor};
    conn.reset_exec_states();
    BOOST_TEST_NOT(conn.is_connected());
}

// Neither does an established connection sitting idle
capy::task<> test_idle_connection()
{
    auto conn = co_await establish_connection();

    conn.reset_exec_states();

    BOOST_TEST(conn.is_connected());
    co_await check_connection_usable(conn);
}

// A state that hasn't been registered yet isn't reset
capy::task<> test_state_not_written()
{
    // Setup
    auto conn = co_await establish_connection();

    request req;
    req.add_query("SELECT $1 AS value", 42);
    check handler;

    // Prepare the state. This doesn't register it yet
    exec_state st;
    if (!BOOST_TEST_EQ(conn.prepare_request(st, req, &handler), std::error_code()))
        co_return;

    // Call the function
    conn.reset_exec_states();

    // Our state is unaffected
    check_status(st, {.is_prepared = true});
    if (!check_success(co_await conn.write_request(st)))
        co_return;
    while (!st.read_done())
    {
        if (!check_success(co_await conn.read_some_response(st)))
            co_return;
    }
    check_success(st.handler_error());

    co_await check_connection_usable(conn);
}

// A state whose response was read in full isn't reset
capy::task<> test_state_finished()
{
    // Setup
    auto conn = co_await establish_connection();

    request req;
    req.add_query("SELECT $1 AS value", 42);
    check handler;

    // Run until completion
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

    // Reset
    conn.reset_exec_states();

    // Untouched
    check_status(st, {.is_prepared = true, .write_phase = write_status::done, .reader_done = true});
    co_await check_connection_usable(conn);
}

// Single attached state
capy::task<> test_one_state()
{
    auto conn = co_await establish_connection();

    request req;
    req.add_query("SELECT $1 AS value", 42);
    check handler;

    // Leave an operation half-done
    exec_state st;
    if (!BOOST_TEST_EQ(conn.prepare_request(st, req, &handler), std::error_code()))
        co_return;
    if (!check_success(co_await conn.write_request(st)))
        co_return;
    check_status(st, {.is_prepared = true, .write_phase = write_status::done, .reader_done = false});

    conn.reset_exec_states();

    // The state is back to default-constructed, and the session survives
    check_status(st, {});
    BOOST_TEST(conn.is_connected());
    co_await check_connection_usable(conn);
}

// Multiple attached states
capy::task<> test_several_states()
{
    // Setup
    auto conn = co_await establish_connection();

    request req;
    req.add_query("SELECT $1 AS value", 42);
    check handler1, handler2;

    // Leave two operations half-dome
    exec_state st1, st2;
    if (!BOOST_TEST_EQ(conn.prepare_request(st1, req, &handler1), std::error_code()))
        co_return;
    if (!BOOST_TEST_EQ(conn.prepare_request(st2, req, &handler2), std::error_code()))
        co_return;
    if (!check_success(co_await conn.write_request(st1)))
        co_return;
    if (!check_success(co_await conn.write_request(st2)))
        co_return;

    conn.reset_exec_states();

    // All of them are reset
    check_status(st1, {});
    check_status(st2, {});

    // Connection usable
    BOOST_TEST(conn.is_connected());
    co_await check_connection_usable(conn);
}

// Resetting twice is a no-op the second time
capy::task<> test_idempotent()
{
    // Setup
    auto conn = co_await establish_connection();

    request req;
    req.add_query("SELECT $1 AS value", 42);
    check handler;

    // Leave an operation half-done
    exec_state st;
    if (!BOOST_TEST_EQ(conn.prepare_request(st, req, &handler), std::error_code()))
        co_return;
    if (!check_success(co_await conn.write_request(st)))
        co_return;

    // Reset twice
    conn.reset_exec_states();
    conn.reset_exec_states();

    // Same as if we had reset only once
    check_status(st, {});
    BOOST_TEST(conn.is_connected());
    co_await check_connection_usable(conn);
}

}  // namespace

int main()
{
    run_coroutine_test(test_never_connected());
    run_coroutine_test(test_idle_connection());

    run_coroutine_test(test_state_not_written());
    run_coroutine_test(test_state_finished());

    run_coroutine_test(test_one_state());
    run_coroutine_test(test_several_states());
    run_coroutine_test(test_idempotent());

    return boost::report_errors();
}
