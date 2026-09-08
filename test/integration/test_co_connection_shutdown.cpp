//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/capy/ex/this_coro.hpp>
#include <boost/capy/io_result.hpp>
#include <boost/capy/task.hpp>
#include <boost/core/lightweight_test.hpp>

#include <system_error>

#include "nativepg/co_connection.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/request.hpp"
#include "nativepg/responses/check.hpp"
#include "test_utils/ci_server.hpp"
#include "test_utils/corosio_utils.hpp"
#include "test_utils/printing.hpp"

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

    // Sanity check: the connection works before shutting it down
    request req;
    req.add_simple_query("SELECT 1");
    if (!check_success(co_await conn.exec(req, check_execute(), &diag), diag))
        co_return;

    // Shut the connection down. This should succeed
    auto [shutdown_ec] = co_await conn.shutdown();
    BOOST_TEST_EQ(shutdown_ec, std::error_code());

    // exec no longer works
    auto [exec_ec] = co_await conn.exec(req, check_execute());
    BOOST_TEST_NE(exec_ec, std::error_code());
}

// Regression test: after shutdown, the connection can be re-opened,
// even if there were unread bytes in the network buffer.
capy::task<> test_shutdown_reopen_pending_bytes()
{
    // Setup
    diagnostics diag;
    co_connection conn{co_await capy::this_coro::executor};
    if (!check_success(co_await conn.connect(default_connect_params(), &diag), diag))
        co_return;

    // Produce a NotificiationResponse that won't be read yet.
    request req;
    req.add_simple_query("LISTEN my_channel");
    req.add_simple_query("NOTIFY my_channel");
    if (!check_success(co_await conn.exec(req, check_execute(), &diag), diag))
        co_return;

    // Shut the connection down
    auto [shutdown_ec] = co_await conn.shutdown();
    BOOST_TEST_EQ(shutdown_ec, std::error_code());

    // We can re-establish the connection
    if (!check_success(co_await conn.connect(default_connect_params(), &diag), diag))
        co_return;

    // The connection works
    request req_select;
    req_select.add_simple_query("SELECT 1");
    if (!check_success(co_await conn.exec(req_select, check_execute(), &diag), diag))
        co_return;
}

}  // namespace

int main()
{
    run_coroutine_test(test_shutdown());
    run_coroutine_test(test_shutdown_reopen_pending_bytes());

    return boost::report_errors();
}
