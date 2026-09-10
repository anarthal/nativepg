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

#include <optional>
#include <system_error>

#include "nativepg/co_connection.hpp"
#include "nativepg/encoding.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/request.hpp"
#include "nativepg/responses/check.hpp"
#include "test_utils/ci_server.hpp"
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

    // Sanity check: the connection works before shutting it down
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
    if (!check_success(co_await conn.exec(req, check(), &diag), diag))
        co_return;
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

}  // namespace

int main()
{
    run_coroutine_test(test_shutdown());
    run_coroutine_test(test_gucs());

    return boost::report_errors();
}
