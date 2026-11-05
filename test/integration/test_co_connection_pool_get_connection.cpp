//
// Copyright (c) 2019-2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/assert/source_location.hpp>
#include <boost/capy/cond.hpp>
#include <boost/capy/ex/async_event.hpp>
#include <boost/capy/ex/run.hpp>
#include <boost/capy/ex/this_coro.hpp>
#include <boost/capy/results.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/when_all.hpp>
#include <boost/capy/when_any.hpp>
#include <boost/core/lightweight_test.hpp>
#include <boost/corosio/timeout.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>

#include "nativepg/co_connection.hpp"
#include "nativepg/co_connection_pool.hpp"
#include "nativepg/exec_state.hpp"
#include "nativepg/protocol/connection_state.hpp"
#include "nativepg/protocol/ready_for_query.hpp"
#include "nativepg/request.hpp"
#include "nativepg/responses/check.hpp"
#include "nativepg/responses/response_handler.hpp"
#include "test_utils/ci_server.hpp"
#include "test_utils/co_connection_utils.hpp"
#include "test_utils/corosio_utils.hpp"
#include "test_utils/printing.hpp"
#include "test_utils/test_cond_eq.hpp"
#include "test_utils/test_opt_eq.hpp"
#include "test_utils/test_string_contains.hpp"
#include "test_utils/yield.hpp"

namespace capy = boost::capy;
namespace corosio = boost::corosio;
using namespace nativepg;
using namespace nativepg::test;
using namespace std::chrono_literals;

namespace {

pool_params create_pool_params(std::size_t max_size = 151)
{
    return {
        .transport = default_connect_params(),
        .max_size = max_size,
    };
}

// Utility to run a test that requires a running pool
capy::task<> with_pool_running(co_connection_pool& pool, capy::task<> body)
{
    static_cast<void>(co_await capy::when_any(
        [&]() -> capy::task<capy::results<>> {
            // Run the pool. If the test passes, the body finishes 1st and cancels run()
            auto [ec] = co_await pool.run();
            test_cond_eq(ec, capy::cond::canceled);
            co_return {};
        }(),
        [&]() -> capy::task<capy::results<>> {
            // Run the test
            co_await std::move(body);
            co_return {};
        }()
    ));
}

// Writes a request but never reads its response, so the connection is healthy
// but owes us a ReadyForQuery
capy::task<bool> create_leftovers(co_connection& conn, boost::source_location loc = BOOST_CURRENT_LOCATION)
{
    request req;
    req.add_query("SELECT $1 AS value", 42);
    check handler;

    exec_state st;
    if (!BOOST_TEST_EQ(conn.prepare_request(st, req, &handler), std::error_code()))
    {
        std::cerr << "  Called from " << loc << std::endl;
        co_return false;
    }
    if (!check_success(co_await conn.write_request(st), loc))
        co_return false;

    st.reset();

    if (!BOOST_TEST(conn.is_connected()) || !BOOST_TEST(conn.has_leftovers()))
    {
        std::cerr << "  Called from " << loc << std::endl;
        co_return false;
    }

    co_return true;
}

// Asks the server to kill the backend serving us. It answers with a FATAL error
// and closes the socket, so the connection ends up in a fatal error state
capy::task<bool> break_connection(co_connection& conn, boost::source_location loc = BOOST_CURRENT_LOCATION)
{
    request req;
    req.add_query("SELECT pg_terminate_backend(pg_backend_pid())");

    auto [ec] = co_await conn.exec(req, check());
    bool ok = BOOST_TEST_NE(ec, std::error_code());
    ok &= BOOST_TEST_NOT(conn.is_connected());
    if (!ok)
        std::cerr << "  Called from " << loc << std::endl;
    co_return ok;
}

// Note: current_setting() returns NULL when a custom setting hasn't been set,
// and the empty string when it has been reset.
// We use optional::value_or() to collapse both cases

// --- Tests ---

// A clean connection returned with a reset request has session state wiped
capy::task<> test_return_with_reset()
{
    co_connection_pool pool{co_await capy::this_coro::executor, create_pool_params(1)};

    co_await with_pool_running(pool, [&]() -> capy::task<> {
        // Get a connection
        auto [ec, conn] = co_await pool.get_connection();
        if (!check_success(ec) || !BOOST_TEST(conn.valid()))
            co_return;
        const auto pid = conn->state().backend_process_id;

        // Alter session state
        if (!co_await set_session_var(conn.get(), "abc"))
            co_return;

        // Return the connection
        conn = pooled_connection();

        // Get another connection
        std::tie(ec, conn) = co_await pool.get_connection();
        if (!check_success(ec) || !BOOST_TEST(conn.valid()))
            co_return;

        // Same session, but session state was reset
        BOOST_TEST_EQ(conn->state().backend_process_id, pid);
        test_opt_eq(co_await get_session_var(conn.get()), std::string());
        co_await check_connection_usable(conn.get());
    }());
}

// pooled_connection's destructor requests a reset
capy::task<> test_pooled_connection_destructor()
{
    // max_size is 1, so the pool can only ever hand us the same connection
    co_connection_pool pool{co_await capy::this_coro::executor, create_pool_params(1)};

    co_await with_pool_running(pool, [&]() -> capy::task<> {
        std::int32_t pid = 0;

        {
            // Get a connection
            auto [ec, conn] = co_await pool.get_connection();
            if (!check_success(ec) || !BOOST_TEST(conn.valid()))
                co_return;
            pid = conn->state().backend_process_id;

            // Alter session state
            if (!co_await set_session_var(conn.get(), "abc"))
                co_return;

            // The destructor returns the connection
        }

        // Get the same connection again
        auto [ec, conn] = co_await pool.get_connection();
        if (!check_success(ec) || !BOOST_TEST(conn.valid()))
            co_return;
        BOOST_TEST_EQ(conn->state().backend_process_id, pid);

        // No reset has been issued, so our mark is still there
        BOOST_TEST_EQ((co_await get_session_var(conn.get())).value_or(""), "abc");
    }());
}

// If we ask for reset not to happen, and we don't break anything, no reset happens
capy::task<> test_return_without_reset()
{
    // max_size is 1, so the pool can only ever hand us the same connection
    co_connection_pool pool{co_await capy::this_coro::executor, create_pool_params(1)};

    co_await with_pool_running(pool, [&]() -> capy::task<> {
        // Get a connection
        auto [ec, conn] = co_await pool.get_connection();
        if (!check_success(ec) || !BOOST_TEST(conn.valid()))
            co_return;
        const auto pid = conn->state().backend_process_id;

        // Alter session state
        if (!co_await set_session_var(conn.get(), "abc"))
            co_return;

        // Return the connection
        conn.return_without_reset();
        BOOST_TEST_NOT(conn.valid());

        // Get the same connection again
        std::tie(ec, conn) = co_await pool.get_connection();
        if (!check_success(ec) || !BOOST_TEST(conn.valid()))
            co_return;
        BOOST_TEST_EQ(conn->state().backend_process_id, pid);

        // No reset has been issued, so our mark is still there
        test_opt_eq(co_await get_session_var(conn.get()), "abc");
    }());
}

// A connection that died while in use is reconnected
capy::task<> test_fatal_error_with_reset()
{
    co_connection_pool pool{co_await capy::this_coro::executor, create_pool_params(1)};

    co_await with_pool_running(pool, [&]() -> capy::task<> {
        auto [ec, conn] = co_await pool.get_connection();
        if (!check_success(ec) || !BOOST_TEST(conn.valid()))
            co_return;
        const auto pid = conn->state().backend_process_id;

        if (!co_await set_session_var(conn.get(), "abc"))
            co_return;
        if (!co_await break_connection(conn.get()))
            co_return;

        // Return the connection
        conn = pooled_connection();

        std::tie(ec, conn) = co_await pool.get_connection();
        if (!check_success(ec) || !BOOST_TEST(conn.valid()))
            co_return;

        // The pool established a new session
        BOOST_TEST_NE(conn->state().backend_process_id, pid);
        test_opt_eq(co_await get_session_var(conn.get()), std::nullopt);
        co_await check_connection_usable(conn.get());
    }());
}

// If the user asked for no reset but the connection died, ignore the user
capy::task<> test_fatal_error_without_reset()
{
    co_connection_pool pool{co_await capy::this_coro::executor, create_pool_params(1)};

    co_await with_pool_running(pool, [&]() -> capy::task<> {
        // Get a connection and mark it
        auto [ec, conn] = co_await pool.get_connection();
        if (!check_success(ec) || !BOOST_TEST(conn.valid()))
            co_return;
        const auto pid = conn->state().backend_process_id;
        if (!co_await set_session_var(conn.get(), "abc"))
            co_return;

        // Generate a fatal error
        if (!co_await break_connection(conn.get()))
            co_return;

        // Return the connection
        conn.return_without_reset();

        // Get the same connection again
        std::tie(ec, conn) = co_await pool.get_connection();
        if (!check_success(ec) || !BOOST_TEST(conn.valid()))
            co_return;

        // It has been re-established
        BOOST_TEST_NE(conn->state().backend_process_id, pid);
        BOOST_TEST_EQ((co_await get_session_var(conn.get())).value_or(""), std::string());
        co_await check_connection_usable(conn.get());
    }());
}

// A connection returned with unread responses is reconnected
capy::task<> test_leftovers_with_reset()
{
    co_connection_pool pool{co_await capy::this_coro::executor, create_pool_params(1)};

    co_await with_pool_running(pool, [&]() -> capy::task<> {
        // Get a connection and mark it
        auto [ec, conn] = co_await pool.get_connection();
        if (!check_success(ec) || !BOOST_TEST(conn.valid()))
            co_return;
        const auto pid = conn->state().backend_process_id;
        if (!co_await set_session_var(conn.get(), "abc"))
            co_return;

        // Produce leftovers
        if (!co_await create_leftovers(conn.get()))
            co_return;

        // Return the connection
        conn = pooled_connection();

        std::tie(ec, conn) = co_await pool.get_connection();
        if (!check_success(ec) || !BOOST_TEST(conn.valid()))
            co_return;

        // We got a new session
        BOOST_TEST_NE(conn->state().backend_process_id, pid);
        BOOST_TEST_EQ((co_await get_session_var(conn.get())).value_or(""), std::string());
        co_await check_connection_usable(conn.get());
    }());
}

// Same, but the user requested no reset. We ignore them.
capy::task<> test_leftovers_without_reset()
{
    co_connection_pool pool{co_await capy::this_coro::executor, create_pool_params(1)};

    co_await with_pool_running(pool, [&]() -> capy::task<> {
        // Get a connection and mark it
        auto [ec, conn] = co_await pool.get_connection();
        if (!check_success(ec) || !BOOST_TEST(conn.valid()))
            co_return;
        const auto pid = conn->state().backend_process_id;
        if (!co_await set_session_var(conn.get(), "abc"))
            co_return;

        // Create leftovers
        if (!co_await create_leftovers(conn.get()))
            co_return;

        // Return the connection
        conn.return_without_reset();

        // Get the same connection
        std::tie(ec, conn) = co_await pool.get_connection();
        if (!check_success(ec) || !BOOST_TEST(conn.valid()))
            co_return;

        // It's a new session
        BOOST_TEST_NE(conn->state().backend_process_id, pid);
        BOOST_TEST_EQ((co_await get_session_var(conn.get())).value_or(""), std::string());
        co_await check_connection_usable(conn.get());
    }());
}

// A connection returned inside a transaction block is rolled back and reset
capy::task<> test_open_txn_with_reset()
{
    co_connection_pool pool{co_await capy::this_coro::executor, create_pool_params(1)};

    co_await with_pool_running(pool, [&]() -> capy::task<> {
        // Get a connection and mark it
        auto [ec, conn] = co_await pool.get_connection();
        if (!check_success(ec) || !BOOST_TEST(conn.valid()))
            co_return;
        const auto pid = conn->state().backend_process_id;
        if (!co_await set_session_var(conn.get(), "abc"))
            co_return;

        // Open a TXN block
        if (!co_await checked_exec(conn.get(), "BEGIN"))
            co_return;
        BOOST_TEST_EQ(conn->txn_status(), protocol::transaction_status::in_transaction);

        // Return the connection
        conn = pooled_connection();

        std::tie(ec, conn) = co_await pool.get_connection();
        if (!check_success(ec) || !BOOST_TEST(conn.valid()))
            co_return;

        // Same session, rolled back and discarded
        BOOST_TEST_EQ(conn->state().backend_process_id, pid);
        BOOST_TEST_EQ(conn->txn_status(), protocol::transaction_status::idle);
        BOOST_TEST_EQ((co_await get_session_var(conn.get())).value_or(""), std::string());
        co_await check_connection_usable(conn.get());
    }());
}

// An open transaction triggers a reset even when the user said none was needed
capy::task<> test_open_txn_without_reset()
{
    co_connection_pool pool{co_await capy::this_coro::executor, create_pool_params(1)};

    co_await with_pool_running(pool, [&]() -> capy::task<> {
        // Get a connection and mark it
        auto [ec, conn] = co_await pool.get_connection();
        if (!check_success(ec) || !BOOST_TEST(conn.valid()))
            co_return;
        const auto pid = conn->state().backend_process_id;
        if (!co_await set_session_var(conn.get(), "abc"))
            co_return;

        // Open a TXN block
        if (!co_await checked_exec(conn.get(), "BEGIN"))
            co_return;
        BOOST_TEST_EQ(conn->txn_status(), protocol::transaction_status::in_transaction);

        // Return the connection
        conn.return_without_reset();

        // Get a new connection
        std::tie(ec, conn) = co_await pool.get_connection();
        if (!check_success(ec) || !BOOST_TEST(conn.valid()))
            co_return;

        // Same session, no transaction, mark gone
        BOOST_TEST_EQ(conn->state().backend_process_id, pid);
        BOOST_TEST_EQ(conn->txn_status(), protocol::transaction_status::idle);
        BOOST_TEST_EQ((co_await get_session_var(conn.get())).value_or(""), std::string());
        co_await check_connection_usable(conn.get());
    }());
}

// Asking for a connection while another one is in use creates a new one
capy::task<> test_connections_created_if_required()
{
    co_connection_pool pool{co_await capy::this_coro::executor, create_pool_params()};

    co_await with_pool_running(pool, [&]() -> capy::task<> {
        // Get a connection
        auto [ec1, conn1] = co_await pool.get_connection();
        if (!check_success(ec1) || !BOOST_TEST(conn1.valid()))
            co_return;
        if (!co_await check_connection_usable(conn1.get()))
            co_return;

        // Get another one. Since the first is still in use, this creates a new connection
        auto [ec2, conn2] = co_await pool.get_connection();
        if (!check_success(ec2) || !BOOST_TEST(conn2.valid()))
            co_return;
        if (!co_await check_connection_usable(conn2.get()))
            co_return;

        // They are backed by different sessions
        BOOST_TEST_NE(conn1->state().backend_process_id, conn2->state().backend_process_id);
    }());
}

// If there are more outstanding requests than pending connections,
// connections are created
capy::task<> test_connections_created_if_requests_gt_pending()
{
    co_connection_pool pool{co_await capy::this_coro::executor, create_pool_params()};

    co_await with_pool_running(pool, [&]() -> capy::task<> {
        // Request several connections in parallel
        auto [ec, conn1, conn2, conn3] = co_await capy::when_all(
            pool.get_connection(),
            pool.get_connection(),
            pool.get_connection()
        );
        if (!check_success(ec))
            co_return;
        if (!BOOST_TEST(conn1.valid()) || !BOOST_TEST(conn2.valid()) || !BOOST_TEST(conn3.valid()))
            co_return;

        // They should be different connections
        BOOST_TEST_NE(conn1->state().backend_process_id, conn2->state().backend_process_id);
        BOOST_TEST_NE(conn1->state().backend_process_id, conn3->state().backend_process_id);
        BOOST_TEST_NE(conn2->state().backend_process_id, conn3->state().backend_process_id);
    }());
}

// The pool never creates more than max_size connections
capy::task<> test_connection_upper_limit()
{
    co_connection_pool pool{co_await capy::this_coro::executor, create_pool_params(1)};

    co_await with_pool_running(pool, [&]() -> capy::task<> {
        // Get the only connection the pool is allowed to create
        auto [ec, conn] = co_await pool.get_connection();
        if (!check_success(ec) || !BOOST_TEST(conn.valid()))
            co_return;

        // Getting another one blocks until we return the one we have.
        // Since we don't, the request times out
        auto [ec2, conn2] = co_await corosio::timeout(pool.get_connection(), 50ms);
        test_cond_eq(ec2, capy::cond::timeout);
        BOOST_TEST_NOT(conn2.valid());
    }());
}

// Connections requested before run() starts wait, rather than failing
capy::task<> test_get_connection_before_run()
{
    co_connection_pool pool{co_await capy::this_coro::executor, create_pool_params()};

    static_cast<void>(co_await capy::when_any(
        [&]() -> capy::task<capy::results<>> {
            // Ask for two connections. The pool isn't running yet, so both wait
            auto [ec, conn1, conn2] = co_await capy::when_all(pool.get_connection(), pool.get_connection());
            if (!check_success(ec))
                co_return {};
            if (!BOOST_TEST(conn1.valid()) || !BOOST_TEST(conn2.valid()))
                co_return {};

            // They're different connections
            BOOST_TEST_NE(conn1->state().backend_process_id, conn2->state().backend_process_id);
            co_return {};
        }(),
        [&]() -> capy::task<capy::results<>> {
            co_await yield();  // give some margin to the tasks
            auto [ec] = co_await pool.run();
            test_cond_eq(ec, capy::cond::canceled);
            co_return {};
        }()
    ));
}

// The pool works with zero timeouts, which disable the
// relevant timer rather than expiring immediately
capy::task<> test_zero_timeouts()
{
    auto params = create_pool_params(1);  // max_size 1
    params.connect_timeout = 0s;
    params.ping_timeout = 0s;
    params.ping_interval = 0s;
    co_connection_pool pool{co_await capy::this_coro::executor, std::move(params)};

    co_await with_pool_running(pool, [&]() -> capy::task<> {
        // Get a connection and use it
        auto [ec, conn] = co_await pool.get_connection();
        if (!check_success(ec))
            co_return;
        if (!BOOST_TEST(conn.valid()))
            co_return;
        co_await check_connection_usable(conn.get());

        // Returning it makes the connection usable again
        conn.return_without_reset();
        auto [ec2, conn2] = co_await pool.get_connection();
        if (!check_success(ec2))
            co_return;
        if (!BOOST_TEST(conn2.valid()))
            co_return;
        co_await check_connection_usable(conn2.get());
    }());
}

// Any get_connection pending when run() is cancelled fails
capy::task<> test_get_connection_during_run_cancel()
{
    co_connection_pool pool{co_await capy::this_coro::executor, create_pool_params(1)};

    std::stop_source run_stop_src;
    capy::async_event finished;

    static_cast<void>(co_await capy::when_all(
        [&]() -> capy::task<capy::results<>> {
            auto [ec] = co_await capy::run(run_stop_src.get_token())(pool.run());
            test_cond_eq(ec, capy::cond::canceled);
            co_return {};
        }(),
        [&]() -> capy::task<capy::results<>> {
            // Get a connection
            auto [ec, conn] = co_await pool.get_connection();
            if (!check_success(ec) || !BOOST_TEST(conn.valid()))
                co_return {};

            // At this point, run is in-progress, and the other get_connection is waiting
            // (because we hold the only connection). Cancel run
            run_stop_src.request_stop();

            // Keep hold of the connection, to ensure that the other task finishes by cancellation
            check_success(co_await finished.wait());
            co_return {};
        }(),
        [&]() -> capy::task<capy::results<>> {
            // Try to get a connection. This should fail because run is canceled
            auto [ec, conn] = co_await pool.get_connection();
            test_cond_eq(ec, capy::cond::canceled);
            BOOST_TEST_NOT(conn.valid());

            // Inform other tasks that we're done
            finished.set();
            co_return {};
        }()
    ));
}

// Any get_connection happening after run() is cancelled fails
capy::task<> test_get_connection_after_run_cancel()
{
    co_connection_pool pool{co_await capy::this_coro::executor, create_pool_params()};

    // Run the pool but exit immediately
    auto [ec] = co_await corosio::timeout(pool.run(), 1ms);
    test_cond_eq(ec, capy::cond::timeout);

    // Subsequent get_connection's fail
    auto [ec2, conn] = co_await pool.get_connection();
    test_cond_eq(ec2, capy::cond::canceled);
    BOOST_TEST_NOT(conn.valid());
}

// get_connection can be canceled
capy::task<> test_cancel_get_connection()
{
    co_connection_pool pool{co_await capy::this_coro::executor, create_pool_params(1)};

    co_await with_pool_running(pool, [&]() -> capy::task<> {
        // Get the only connection
        auto [ec1, conn1] = co_await pool.get_connection();
        if (!check_success(ec1) || !BOOST_TEST(conn1.valid()))
            co_return;
        co_await check_connection_usable(conn1.get());

        // Try to get a connection on a stopped source.
        // This might succeed if there are available connections (but there aren't at this point)
        std::stop_source src;
        src.request_stop();
        auto [ec2, conn2] = co_await capy::run(src.get_token())(pool.get_connection());
        test_cond_eq(ec2, capy::cond::canceled);
        BOOST_TEST_NOT(conn2.valid());

        // A cancellation doesn't cause problems: other requests can succeed
        conn1.return_without_reset();
        std::tie(ec1, conn1) = co_await pool.get_connection();
        if (!check_success(ec1) || !BOOST_TEST(conn1.valid()))
            co_return;
        co_await check_connection_usable(conn1.get());
    }());
}

// Spotcheck: constructing a pool with invalid params throws
capy::task<> test_invalid_params()
{
    auto ex = co_await capy::this_coro::executor;

    auto check_throws = [ex](pool_params params, std::string_view expected_msg) {
        try
        {
            static_cast<void>(co_connection_pool(ex, std::move(params)));
            BOOST_ERROR("Expected std::invalid_argument, but no exception was thrown");
        }
        catch (const std::invalid_argument& exc)
        {
            test_string_contains(exc.what(), expected_msg);
        }
    };

    check_throws({.transport = {}, .max_size = 0}, "pool_params::max_size must be greater than zero");
    check_throws(
        {.transport = {}, .initial_size = 4, .max_size = 2},
        "pool_params::max_size must be greater than pool_params::initial_size"
    );
    check_throws(
        {.transport = {}, .connect_timeout = -1s},
        "pool_params::connect_timeout must not be negative"
    );
    check_throws(
        {.transport = {}, .retry_interval = 0s},
        "pool_params::retry_interval must be greater than zero"
    );
    check_throws(
        {.transport = {}, .retry_interval = -1s},
        "pool_params::retry_interval must be greater than zero"
    );
    check_throws({.transport = {}, .ping_interval = -1s}, "pool_params::ping_interval must not be negative");
    check_throws({.transport = {}, .ping_timeout = -1s}, "pool_params::ping_timeout must not be negative");
}

}  // namespace

int main()
{
    run_coroutine_test(test_return_with_reset());
    run_coroutine_test(test_pooled_connection_destructor());
    run_coroutine_test(test_return_without_reset());
    run_coroutine_test(test_fatal_error_with_reset());
    run_coroutine_test(test_fatal_error_without_reset());
    run_coroutine_test(test_leftovers_with_reset());
    run_coroutine_test(test_leftovers_without_reset());
    run_coroutine_test(test_open_txn_with_reset());
    run_coroutine_test(test_open_txn_without_reset());

    run_coroutine_test(test_connections_created_if_required());
    run_coroutine_test(test_connections_created_if_requests_gt_pending());
    run_coroutine_test(test_connection_upper_limit());
    run_coroutine_test(test_get_connection_before_run());
    run_coroutine_test(test_zero_timeouts());

    run_coroutine_test(test_get_connection_during_run_cancel());
    run_coroutine_test(test_get_connection_after_run_cancel());
    run_coroutine_test(test_cancel_get_connection());
    run_coroutine_test(test_invalid_params());

    return boost::report_errors();
}
