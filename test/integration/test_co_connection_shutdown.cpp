//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/capy/ex/this_coro.hpp>
#include <boost/capy/task.hpp>
#include <boost/core/lightweight_test.hpp>

#include <optional>
#include <system_error>

#include "nativepg/co_connection.hpp"
#include "nativepg/encoding.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/request.hpp"
#include "nativepg/responses/check.hpp"
#include "test_utils/ci_server.hpp"
#include "test_utils/co_connection_utils.hpp"
#include "test_utils/corosio_utils.hpp"
#include "test_utils/printing.hpp"
#include "test_utils/test_opt_eq.hpp"

namespace capy = boost::capy;
using namespace nativepg;
using namespace nativepg::test;

namespace {

// After shutdown, the connection is unusable: exec fails and performs no I/O
capy::task<> test_shutdown()
{
    // Setup
    diagnostics diag;
    co_connection conn{co_await capy::this_coro::executor};
    if (!check_success(co_await conn.connect(default_connect_params(), &diag), diag))
        co_return;

    // The connection works
    request req;
    req.add_simple_query("SELECT 1");
    if (!check_success(co_await conn.exec(req, check(), &diag), diag))
        co_return;

    // Shut the connection down. This should succeed
    auto [shutdown_ec] = co_await conn.shutdown();
    BOOST_TEST_EQ(shutdown_ec, std::error_code());

    // exec no longer works
    auto [exec_ec] = co_await conn.exec(req, check());
    BOOST_TEST_NE(exec_ec, std::error_code());

    // The connection can be re-opened
    if (!check_success(co_await conn.connect(default_connect_params(), &diag), diag))
        co_return;
    co_await check_connection_usable(conn);
}

// shutdown invalidates any GUC we had recorded
capy::task<> test_gucs()
{
    // Setup
    diagnostics diag;
    co_connection conn{co_await capy::this_coro::executor};
    if (!check_success(co_await conn.connect(default_connect_params(), &diag), diag))
        co_return;
    test_opt_eq(conn.client_encoding(), encoding::utf8);

    // Shut the connection down
    auto [shutdown_ec] = co_await conn.shutdown();
    BOOST_TEST_EQ(shutdown_ec, std::error_code());

    // Check
    test_opt_eq(conn.client_encoding(), std::nullopt);
}

// shutdown can be called on a not-yet-established connection
capy::task<> test_shutdown_not_connected()
{
    // Setup
    co_connection conn{co_await capy::this_coro::executor};
    BOOST_TEST_NOT(conn.is_connected());

    // Should be a no-op
    auto [shutdown_ec] = co_await conn.shutdown();
    BOOST_TEST_EQ(shutdown_ec, std::error_code());
    BOOST_TEST_NOT(conn.is_connected());
    BOOST_TEST_EQ(conn.last_error(), std::error_code());

    // The connection can still be established afterwards
    diagnostics diag;
    if (!check_success(co_await conn.connect(default_connect_params(), &diag), diag))
        co_return;
    co_await check_connection_usable(conn);
}

// shutdown cleans up after a fatal error, without trying to talk to the server
capy::task<> test_shutdown_after_fatal_error()
{
    // Setup
    diagnostics diag;
    auto conn = co_await establish_connection();

    // Ask the server to kill the backend serving us. It answers with a FATAL
    // error and closes the connection, so our reader never gets its
    // ReadyForQuery and the operation fails
    request req_kill;
    req_kill.add_simple_query("SELECT pg_terminate_backend(pg_backend_pid())");
    auto [exec_ec] = co_await conn.exec(req_kill, check());
    BOOST_TEST_NE(exec_ec, std::error_code());
    BOOST_TEST_NOT(conn.is_connected());
    BOOST_TEST_NE(conn.last_error(), std::error_code());

    // Shutting down succeeds: the Terminate message is skipped
    if (!check_success(co_await conn.shutdown()))
        co_return;
    BOOST_TEST_NOT(conn.is_connected());
    BOOST_TEST_EQ(conn.last_error(), std::error_code());  // was cleared

    // The connection can be re-established
    if (!check_success(co_await conn.connect(default_connect_params(), &diag), diag))
        co_return;
    co_await check_connection_usable(conn);
}

}  // namespace

int main()
{
    run_coroutine_test(test_shutdown());
    run_coroutine_test(test_gucs());
    run_coroutine_test(test_shutdown_not_connected());
    run_coroutine_test(test_shutdown_after_fatal_error());

    return boost::report_errors();
}
