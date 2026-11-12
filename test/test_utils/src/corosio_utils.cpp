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
#include <boost/capy/when_any.hpp>
#include <boost/core/lightweight_test.hpp>
#include <boost/corosio/io_context.hpp>
#include <boost/describe/class.hpp>
#include <boost/describe/operators.hpp>

#include <chrono>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "nativepg/co_connection_pool.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/request.hpp"
#include "nativepg/responses/into.hpp"
#include "test_utils/co_connection_pool_utils.hpp"
#include "test_utils/co_connection_utils.hpp"
#include "test_utils/corosio_utils.hpp"
#include "test_utils/printing.hpp"
#include "test_utils/test_cond_eq.hpp"
#include "test_utils/test_range_eq.hpp"

namespace capy = boost::capy;
namespace corosio = boost::corosio;

namespace {

struct row_int
{
    int value;
};
BOOST_DESCRIBE_STRUCT(row_int, (), (value))

struct row_nullable_string
{
    std::optional<std::string> value;
};
BOOST_DESCRIBE_STRUCT(row_nullable_string, (), (value))

using boost::describe::operators::operator==;
using boost::describe::operators::operator<<;

// The name is shared by every test that uses these helpers.
// OK because the setting is session-local
constexpr std::string_view session_var_name = "nativepg.test_var";

}  // namespace

void nativepg::test::run_coroutine_test(capy::task<void> test, boost::source_location loc)
{
    // Set a timeout to the tests, so they don't hang on error
    constexpr std::chrono::seconds test_timeout{10};
    bool finished = false;
    auto wrapper_fn = [test = std::move(test), &finished]() mutable -> capy::task<void> {
        co_await std::move(test);
        finished = true;
    };

    // Actually run the test
    corosio::io_context ctx;
    capy::run_async(ctx.get_executor())(wrapper_fn());
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

capy::task<nativepg::co_connection> nativepg::test::establish_connection(
    const connect_params& params,
    boost::source_location loc
)
{
    co_connection conn{co_await capy::this_coro::executor};

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

capy::task<bool> nativepg::test::checked_exec(
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

capy::task<bool> nativepg::test::check_connection_usable(co_connection& conn, boost::source_location loc)
{
    request req;
    req.add_query("SELECT $1 AS value", 1234);
    std::vector<row_int> ints;
    if (!co_await checked_exec(conn, req, into(ints), loc))
        co_return false;
    co_return test_range_eq(ints, std::vector<row_int>{{.value = 1234}}, loc);
}

std::string nativepg::test::set_session_var_query(std::string_view value)
{
    return std::string("SET ").append(session_var_name).append(" = '").append(value).append("'");
}

capy::task<bool> nativepg::test::set_session_var(
    co_connection& conn,
    std::string_view value,
    boost::source_location loc
)
{
    co_return co_await checked_exec(conn, set_session_var_query(value), loc);
}

capy::task<std::optional<std::string>> nativepg::test::get_session_var(
    co_connection& conn,
    boost::source_location loc
)
{
    // The second argument makes current_setting return NULL, rather than failing,
    // when the variable was never set in this session
    request req;
    req.add_query(
        std::string("SELECT current_setting('").append(session_var_name).append("', true) AS value")
    );

    std::vector<row_nullable_string> rows;
    if (!co_await checked_exec(conn, req, into(rows), loc))
        co_return std::nullopt;
    if (!BOOST_TEST_EQ(rows.size(), 1u))
    {
        std::cerr << "  Called from " << loc << std::endl;
        co_return std::nullopt;
    }
    co_return std::move(rows[0].value);
}

capy::task<bool> nativepg::test::break_connection(co_connection& conn, boost::source_location loc)
{
    request req;
    req.add_query("SELECT pg_terminate_backend(pg_backend_pid())");

    auto [ec] = co_await conn.exec(req, check());
    bool ok = BOOST_TEST_NE(ec, std::error_code());  // exact code unreliable
    ok &= BOOST_TEST_NOT(conn.is_connected());
    if (!ok)
        std::cerr << "  Called from " << loc << std::endl;
    co_return ok;
}

capy::task<bool> nativepg::test::cache_notification(
    co_connection& conn,
    std::string_view channel,
    std::string_view payload,
    boost::source_location loc
)
{
    request req;
    req.add_query("NOTIFY " + std::string(channel) + ", '" + std::string(payload) + "'");
    req.add_query("SELECT 1");
    co_return co_await checked_exec(conn, req, check(), loc);
}

capy::task<> nativepg::test::with_pool_running(co_connection_pool& pool, capy::task<> body)
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
