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

// Tests that we process each reported GUC correctly.
// Doesn't test all possible functions that may mutate GUCs - this should be
// tested by each function's tests.

capy::task<> test_standard_conforming_strings()
{
    // Setup
    diagnostics diag;
    co_connection conn{co_await capy::this_coro::executor};

    // Not connected yet, so we know nothing
    test_opt_eq(conn.standard_conforming_strings(), std::nullopt);

    // Connecting reports the server's default
    if (!check_success(co_await conn.connect(default_connect_params(), &diag), diag))
        co_return;
    test_opt_eq(conn.standard_conforming_strings(), true);

    // Changing the value is picked up
    request req;
    req.add_simple_query("SET standard_conforming_strings TO off");
    if (!check_success(co_await conn.exec(req, check(), &diag), diag))
        co_return;
    test_opt_eq(conn.standard_conforming_strings(), false);

    // Shutting down invalidates the value
    auto [shutdown_ec] = co_await conn.shutdown();
    BOOST_TEST_EQ(shutdown_ec, std::error_code());
    test_opt_eq(conn.standard_conforming_strings(), std::nullopt);
}

capy::task<> test_client_encoding()
{
    // Setup
    diagnostics diag;
    co_connection conn{co_await capy::this_coro::executor};

    // Not connected yet, so we know nothing
    test_opt_eq(conn.client_encoding(), std::nullopt);

    // Connecting reports the server's default
    if (!check_success(co_await conn.connect(default_connect_params(), &diag), diag))
        co_return;
    test_opt_eq(conn.client_encoding(), encoding::utf8);

    // Changing the value is picked up.
    // LATIN1 is chosen because the server can convert to it from UTF8.
    request req;
    req.add_simple_query("SET client_encoding TO 'LATIN1'");
    if (!check_success(co_await conn.exec(req, check(), &diag), diag))
        co_return;
    test_opt_eq(conn.client_encoding(), encoding::latin1);

    // Shutting down invalidates the value
    auto [shutdown_ec] = co_await conn.shutdown();
    BOOST_TEST_EQ(shutdown_ec, std::error_code());
    test_opt_eq(conn.client_encoding(), std::nullopt);
}

}  // namespace

int main()
{
    run_coroutine_test(test_standard_conforming_strings());
    run_coroutine_test(test_client_encoding());

    return boost::report_errors();
}
