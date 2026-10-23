//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/capy/cond.hpp>
#include <boost/capy/error.hpp>
#include <boost/capy/ex/async_event.hpp>
#include <boost/capy/ex/run.hpp>
#include <boost/capy/ex/this_coro.hpp>
#include <boost/capy/results.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/when_all.hpp>
#include <boost/core/lightweight_test.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <system_error>

#include "nativepg/client_errc.hpp"
#include "nativepg/co_connection.hpp"
#include "nativepg/encoding.hpp"
#include "nativepg/exec_state.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/protocol/connection_state.hpp"
#include "nativepg/request.hpp"
#include "nativepg/responses/check.hpp"
#include "nativepg/sqlstate_cond.hpp"
#include "test_utils/ci_server.hpp"
#include "test_utils/co_connection_utils.hpp"
#include "test_utils/corosio_utils.hpp"
#include "test_utils/printing.hpp"
#include "test_utils/test_cond_eq.hpp"
#include "test_utils/test_opt_eq.hpp"
#include "test_utils/test_string_contains.hpp"
#include "test_utils/yield.hpp"

namespace capy = boost::capy;
using namespace nativepg;
using namespace nativepg::test;

namespace {

// Sunny-day case
capy::task<> test_success()
{
    // Setup
    diagnostics diag;
    co_connection conn{co_await capy::this_coro::executor};
    BOOST_TEST_NOT(conn.is_connected());

    // Connect
    if (!check_success(co_await conn.connect(default_connect_params(), &diag), diag))
        co_return;

    // The GUCs sent by the server were updated
    test_opt_eq(conn.client_encoding(), encoding::utf8);

    // We are now connected
    BOOST_TEST(conn.is_connected());
    BOOST_TEST_EQ(conn.last_error(), std::error_code());

    co_await check_connection_usable(conn);
}

capy::task<> test_server_error()
{
    // Setup: credentials that the server won't accept
    auto params = default_connect_params();
    params.password = "bad_password";

    diagnostics diag;
    co_connection conn{co_await capy::this_coro::executor};

    // Connect fails with the error the server sent us
    auto [ec] = co_await conn.connect(params, &diag);
    test_cond_eq(ec, sqlstate_cond::invalid_password);
    test_string_contains(diag.message(), "28P01");

    // We're not connected, and the failure was recorded
    BOOST_TEST_NOT(conn.is_connected());
    BOOST_TEST_EQ(conn.last_error(), ec);

    // Reconnection is possible (e.g. if the credentials become valid)
    if (!check_success(co_await conn.connect(default_connect_params(), &diag), diag))
        co_return;
    BOOST_TEST(conn.is_connected());
    BOOST_TEST_EQ(conn.last_error(), std::error_code());

    co_await check_connection_usable(conn);
}

// A connection that was shut down cleanly can be re-established
capy::task<> test_connect_after_shutdown()
{
    // Setup
    auto conn = co_await establish_connection();
    const auto old_pid = conn.state().backend_process_id;
    diagnostics diag;

    // Use the connection
    if (!co_await checked_exec(conn, "SELECT 42"))
        co_return;

    // Shut the connection down
    if (!check_success(co_await conn.shutdown()))
        co_return;
    BOOST_TEST_NOT(conn.is_connected());

    // Connect again
    if (!check_success(co_await conn.connect(default_connect_params(), &diag), diag))
        co_return;

    // We're in a brand new session
    BOOST_TEST(conn.is_connected());
    BOOST_TEST_EQ(conn.last_error(), std::error_code());
    BOOST_TEST_NE(conn.state().backend_process_id, old_pid);

    co_await check_connection_usable(conn);
}

// A connection that hit a fatal error can be re-established
capy::task<> test_connect_after_fatal_error()
{
    // Setup
    auto conn = co_await establish_connection();
    const auto old_pid = conn.state().backend_process_id;

    // Ask the server to kill the backend serving us. It answers with a FATAL
    // error and closes the connection, so our reader never sees its
    // ReadyForQuery and the read fails
    request req;
    req.add_query("SELECT pg_terminate_backend(pg_backend_pid())");
    auto [ec] = co_await conn.exec(req, check());
    BOOST_TEST_NE(ec, std::error_code());

    // The connection is no longer usable, and remembers why
    BOOST_TEST_NOT(conn.is_connected());
    BOOST_TEST_EQ(conn.last_error(), ec);

    // Reconnect
    diagnostics diag;
    if (!check_success(co_await conn.connect(default_connect_params(), &diag), diag))
        co_return;
    BOOST_TEST(conn.is_connected());
    BOOST_TEST_EQ(conn.last_error(), std::error_code());
    BOOST_TEST_NE(conn.state().backend_process_id, old_pid);

    co_await check_connection_usable(conn);
}

// If there were leftovers from previous sessions, connect() discards them
capy::task<> test_connect_removes_leftovers()
{
    // Setup
    auto conn = co_await establish_connection();

    // Write a request and abandon it without reading its response
    request req;
    req.add_query("SELECT $1 AS value", 42);
    check handler;

    exec_state st;
    if (!BOOST_TEST_EQ(conn.prepare_request(st, req, &handler), std::error_code()))
        co_return;
    if (!check_success(co_await conn.write_request(st)))
        co_return;
    st.reset();

    // End the session, then re-establish the connection
    if (!check_success(co_await conn.shutdown()))
        co_return;
    diagnostics diag;
    if (!check_success(co_await conn.connect(default_connect_params(), &diag), diag))
        co_return;

    co_await check_connection_usable(conn);
}

// connect() on an established connection replaces the session
capy::task<> test_connect_while_connected()
{
    // Setup
    auto conn = co_await establish_connection();
    const auto old_pid = conn.state().backend_process_id;

    // Connect again, without shutting down first
    diagnostics diag;
    if (!check_success(co_await conn.connect(default_connect_params(), &diag), diag))
        co_return;

    // We're in a new session
    BOOST_TEST(conn.is_connected());
    BOOST_TEST_EQ(conn.last_error(), std::error_code());
    BOOST_TEST_NE(conn.state().backend_process_id, old_pid);

    co_await check_connection_usable(conn);
}

// connect() on an established connection cancels operations in flight
capy::task<> test_connect_while_connected_cancels_ops()
{
    // Setup
    auto conn = co_await establish_connection();
    diagnostics diag;
    const auto pid = conn.state().backend_process_id;

    // A request that won't finish by itself
    request req;
    req.add_query("SELECT pg_sleep(9999)");

    static_cast<void>(co_await capy::when_all(
        [&]() -> capy::task<capy::results<>> {
            // Read until we get the cancellation
            auto [ec] = co_await conn.exec(req, check());
            test_cond_eq(ec, capy::cond::canceled);
            co_return {};
        }(),

        [&]() -> capy::task<capy::results<>> {
            co_await yield();  // just in case, so exec() starts
            check_success(co_await conn.connect(default_connect_params(), &diag), diag);
            co_return {};
        }()
    ));

    // The replacement session is new and healthy
    BOOST_TEST(conn.is_connected());
    BOOST_TEST_EQ(conn.last_error(), std::error_code());
    BOOST_TEST_NE(conn.state().backend_process_id, pid);

    co_await check_connection_usable(conn);
}

// A connect() that fails on an established connection leaves it closed,
// rather than restoring the session it replaced
capy::task<> test_connect_while_connected_failure()
{
    // Setup
    auto conn = co_await establish_connection();

    // Connect again, with credentials that the server won't accept
    auto params = default_connect_params();
    params.password = "bad_password";

    diagnostics diag;
    auto [ec] = co_await conn.connect(params, &diag);
    test_cond_eq(ec, sqlstate_cond::invalid_password);

    // The old session is gone, and the failure was recorded
    BOOST_TEST_NOT(conn.is_connected());
    BOOST_TEST_EQ(conn.last_error(), ec);

    // The connection can still be re-established
    if (!check_success(co_await conn.connect(default_connect_params(), &diag), diag))
        co_return;
    co_await check_connection_usable(conn);
}

// Two connect() operations can't run in parallel
capy::task<> test_connect_while_connect()
{
    // Setup
    co_connection conn{co_await capy::this_coro::executor};

    // Recall that when_all launches things in order, so the first connect
    // is in-flight by the time the second one runs
    static_cast<void>(co_await capy::when_all(
        [&]() -> capy::task<capy::results<>> {
            diagnostics diag;
            check_success(co_await conn.connect(default_connect_params(), &diag), diag);
            co_return {};
        }(),

        [&]() -> capy::task<capy::results<>> {
            auto [ec] = co_await conn.connect(default_connect_params());
            BOOST_TEST_EQ(ec, std::error_code(client_errc::already_running));
            co_return {};
        }()
    ));

    // The rejected attempt didn't disturb the one that won
    BOOST_TEST(conn.is_connected());
    co_await check_connection_usable(conn);
}

// connect() can't run while a shutdown() is in progress
capy::task<> test_connect_while_shutdown()
{
    // Setup
    auto conn = co_await establish_connection();

    // Recall that when_all launches things in order
    static_cast<void>(co_await capy::when_all(
        [&]() -> capy::task<capy::results<>> {
            check_success(co_await conn.shutdown());
            co_return {};
        }(),
        [&]() -> capy::task<capy::results<>> {
            auto [ec] = co_await conn.connect(default_connect_params());
            BOOST_TEST_EQ(ec, make_error_code(client_errc::already_running));
            co_return {};
        }()
    ));

    // The shutdown completed, and the connection can be re-established
    BOOST_TEST_NOT(conn.is_connected());
    diagnostics diag;
    if (!check_success(co_await conn.connect(default_connect_params(), &diag), diag))
        co_return;
    co_await check_connection_usable(conn);
}

}  // namespace

int main()
{
    run_coroutine_test(test_success());
    run_coroutine_test(test_server_error());
    run_coroutine_test(test_connect_after_shutdown());
    run_coroutine_test(test_connect_after_fatal_error());
    run_coroutine_test(test_connect_removes_leftovers());

    run_coroutine_test(test_connect_while_connected());
    run_coroutine_test(test_connect_while_connected_cancels_ops());
    run_coroutine_test(test_connect_while_connected_failure());

    run_coroutine_test(test_connect_while_connect());
    run_coroutine_test(test_connect_while_shutdown());

    return boost::report_errors();
}
