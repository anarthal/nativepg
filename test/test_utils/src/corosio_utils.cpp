//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/assert/source_location.hpp>
#include <boost/capy/ex/run_async.hpp>
#include <boost/capy/ex/this_coro.hpp>
#include <boost/capy/task.hpp>
#include <boost/core/lightweight_test.hpp>
#include <boost/corosio/io_context.hpp>
#include <boost/describe/class.hpp>
#include <boost/describe/operators.hpp>

#include <chrono>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <system_error>
#include <utility>

#include "nativepg/extended_error.hpp"
#include "nativepg/request.hpp"
#include "nativepg/responses/into.hpp"
#include "test_utils/co_connection_utils.hpp"
#include "test_utils/corosio_utils.hpp"
#include "test_utils/printing.hpp"
#include "test_utils/test_range_eq.hpp"

namespace {

struct row_int
{
    int value;
};
BOOST_DESCRIBE_STRUCT(row_int, (), (value))

using boost::describe::operators::operator==;
using boost::describe::operators::operator<<;

}  // namespace

void nativepg::test::run_coroutine_test(boost::capy::task<void> test, boost::source_location loc)
{
    // Set a timeout to the tests, so they don't hang on error
    constexpr std::chrono::seconds test_timeout{10};
    bool finished = false;
    auto wrapper_fn = [test = std::move(test), &finished]() mutable -> boost::capy::task<void> {
        co_await std::move(test);
        finished = true;
    };

    // Actually run the test
    boost::corosio::io_context ctx;
    boost::capy::run_async(ctx.get_executor())(wrapper_fn());
    ctx.run_for(test_timeout);

    // Check that it finished
    if (!BOOST_TEST(finished))
        std::cerr << "  Called from " << loc << std::endl;
}

bool nativepg::test::check_success(std::error_code ec, const diagnostics& diag, boost::source_location loc)
{
    bool ok = BOOST_TEST_EQ(ec, std::error_code());
    ok = BOOST_TEST_EQ(diag, diagnostics()) && ok;
    if (!ok)
        std::cerr << "  Called from " << loc << std::endl;
    return ok;
}

boost::capy::task<nativepg::co_connection> nativepg::test::establish_connection(
    const connect_params& params,
    boost::source_location loc
)
{
    co_connection conn{co_await boost::capy::this_coro::executor};

    diagnostics diag;
    if (!check_success(co_await conn.connect(params, &diag), diag, loc))
    {
        // Without a connection there is nothing left for the test to do, and handing
        // back an unusable one would turn a single failure into a cascade of them
        std::cerr << "Could not establish a connection to the server, aborting" << std::endl;
        std::terminate();
    }

    co_return std::move(conn);
}

boost::capy::task<bool> nativepg::test::checked_exec(
    co_connection& conn,
    const request& req,
    response_handler_ref handler,
    boost::source_location loc
)
{
    diagnostics diag;
    auto [ec] = co_await conn.exec(req, handler, &diag);
    co_return check_success(ec, diag, loc);
}

boost::capy::task<void> nativepg::test::check_connection_usable(
    co_connection& conn,
    boost::source_location loc
)
{
    request req;
    req.add_query("SELECT $1 AS value", 1234);
    std::vector<row_int> ints;
    if (co_await checked_exec(conn, req, into(ints), loc))
        test_range_eq(ints, std::vector<row_int>{{.value = 1234}}, loc);
}
