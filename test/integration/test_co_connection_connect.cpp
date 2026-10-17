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
#include "nativepg/encoding.hpp"
#include "nativepg/extended_error.hpp"
#include "test_utils/ci_server.hpp"
#include "test_utils/corosio_utils.hpp"
#include "test_utils/printing.hpp"
#include "test_utils/test_opt_eq.hpp"

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
}

}  // namespace

int main()
{
    run_coroutine_test(test_success());

    return boost::report_errors();
}
