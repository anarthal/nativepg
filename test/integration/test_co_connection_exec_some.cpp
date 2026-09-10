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

// exec_some processes any GUC reported while reading the response
capy::task<> test_gucs()
{
    // Setup
    diagnostics diag;
    co_connection conn{co_await capy::this_coro::executor};
    if (!check_success(co_await conn.connect(default_connect_params(), &diag), diag))
        co_return;

    // Change a GUC. req and the handler must be kept alive until we finish executing
    request req;
    req.add_simple_query("SET client_encoding TO 'LATIN1'");
    check handler;
    conn.setup_request(req, &handler);

    // Read the entire response
    while (true)
    {
        auto [ec, res] = co_await conn.exec_some();
        if (!BOOST_TEST_EQ(ec, std::error_code()))
            co_return;
        if (res.type() == exec_some_result::kind::done)
            break;
    }

    // Check
    test_opt_eq(conn.client_encoding(), encoding::latin1);
}

}  // namespace

int main()
{
    run_coroutine_test(test_gucs());

    return boost::report_errors();
}
