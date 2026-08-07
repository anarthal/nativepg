//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

// exec() cases covering named statement usage

#include <boost/assert/source_location.hpp>
#include <boost/capy/ex/run_async.hpp>
#include <boost/capy/ex/this_coro.hpp>
#include <boost/capy/io_result.hpp>
#include <boost/capy/task.hpp>
#include <boost/core/lightweight_test.hpp>
#include <boost/describe/class.hpp>
#include <boost/describe/operators.hpp>

#include <cstdint>
#include <iterator>
#include <string>
#include <vector>

#include "nativepg/co_connection.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/request.hpp"
#include "nativepg/response.hpp"
#include "nativepg/sqlstate_cond.hpp"
#include "test_utils/ci_server.hpp"
#include "test_utils/corosio_utils.hpp"
#include "test_utils/printing.hpp"

namespace capy = boost::capy;
using namespace nativepg;
using namespace nativepg::test;

namespace {

struct row_type
{
    int f1;
    std::string f2;
};
BOOST_DESCRIBE_STRUCT(row_type, (), (f1, f2))

struct row_string
{
    std::string value;
};
BOOST_DESCRIBE_STRUCT(row_string, (), (value))

using boost::describe::operators::operator==;
using boost::describe::operators::operator<<;

// The version with compile-time parameters works
capy::task<> test_static_params()
{
    // Setup
    diagnostics diag;
    co_connection conn{co_await capy::this_coro::executor};
    if (!check_success(co_await conn.connect(default_connect_params(), &diag), diag))
        co_return;
    statement<std::int32_t, std::string> stmt{"my_stmt"};

    // Prepare
    request req_prepare;
    req_prepare.add_prepare("SELECT $1 AS f1, $2 AS f2", stmt);
    if (!check_success(co_await conn.exec(req_prepare, check(), &diag), diag))
        co_return;

    // TODO: can we check that we actually prepared the right thing?

    // Execute it
    request req_execute;
    req_execute.add_execute(stmt.bind(42, "abc"));
    std::vector<row_type> rows;
    if (!check_success(co_await conn.exec(req_execute, into(rows), &diag), diag))
        co_return;

    const row_type expected[] = {
        {42, "abc"}
    };
    BOOST_TEST_ALL_EQ(rows.begin(), rows.end(), std::begin(expected), std::end(expected));
}

// The version with dynamic parameters works, and uses text by default
// for the parameters because the user may not pass OIDs
capy::task<> test_dynamic_params_without_oids()
{
    // Setup
    diagnostics diag;
    co_connection conn{co_await capy::this_coro::executor};
    if (!check_success(co_await conn.connect(default_connect_params(), &diag), diag))
        co_return;

    // Prepare. Without the explicit cast, Postgres interprets the number as a string
    // TODO: I don't really like this. You get type coercions by default.
    // It will work in most cases, but if you specify 42 (and not "42"), it's because of something.
    // What are we, javascript?
    request req_prepare;
    req_prepare.add_prepare("SELECT $1::int AS f1, $2 AS f2", "my_stmt");
    if (!check_success(co_await conn.exec(req_prepare, check(), &diag), diag))
        co_return;

    // Execute it
    request req_execute;
    req_execute.add_execute("my_stmt", {42, "abc"});
    std::vector<row_type> rows;
    if (!check_success(co_await conn.exec(req_execute, into(rows), &diag), diag))
        co_return;

    const row_type expected[] = {
        {42, "abc"}
    };
    BOOST_TEST_ALL_EQ(rows.begin(), rows.end(), std::begin(expected), std::end(expected));
}

// You may specify the type OIDs of the params by hand
capy::task<> test_dynamic_params_with_oids()
{
    // Setup
    diagnostics diag;
    co_connection conn{co_await capy::this_coro::executor};
    if (!check_success(co_await conn.connect(default_connect_params(), &diag), diag))
        co_return;

    // Prepare.
    // TODO: is this really a use case? If we had known the types at compile-time,
    // we should have used the typed version. If not, we probably only have the serialized versions.
    request req_prepare;
    constexpr std::int32_t oids[] = {23, 25};
    req_prepare.add_prepare("SELECT $1 AS f1, $2 AS f2", "my_stmt", oids);
    if (!check_success(co_await conn.exec(req_prepare, check(), &diag), diag))
        co_return;

    // Execute it. You might use binary as well here
    request req_execute;
    req_execute.add_execute("my_stmt", {42, "abc"}, request::param_format::select_best);
    std::vector<row_type> rows;
    if (!check_success(co_await conn.exec(req_execute, into(rows), &diag), diag))
        co_return;

    const row_type expected[] = {
        {42, "abc"}
    };
    BOOST_TEST_ALL_EQ(rows.begin(), rows.end(), std::begin(expected), std::end(expected));
}

// Statements without parameters work (although this is really uncommon)
capy::task<> test_no_params()
{
    // Setup
    diagnostics diag;
    co_connection conn{co_await capy::this_coro::executor};
    if (!check_success(co_await conn.connect(default_connect_params(), &diag), diag))
        co_return;

    // Prepare
    request req_prepare;
    req_prepare.add_prepare("SELECT 42 AS f1, 'abc' AS f2", "my_stmt");
    if (!check_success(co_await conn.exec(req_prepare, check(), &diag), diag))
        co_return;

    // Execute it. You might use binary as well here
    request req_execute;
    req_execute.add_execute("my_stmt", {});
    std::vector<row_type> rows;
    if (!check_success(co_await conn.exec(req_execute, into(rows), &diag), diag))
        co_return;

    const row_type expected[] = {
        {42, "abc"}
    };
    BOOST_TEST_ALL_EQ(rows.begin(), rows.end(), std::begin(expected), std::end(expected));
}

// Closing a statement works
capy::task<> test_close()
{
    // Setup
    diagnostics diag;
    co_connection conn{co_await capy::this_coro::executor};
    if (!check_success(co_await conn.connect(default_connect_params(), &diag), diag))
        co_return;

    // Prepare and execute to check that it works
    request req_prepare;
    req_prepare.add_prepare("SELECT 42 AS f1, 'abc' AS f2", "my_stmt");
    req_prepare.add_execute("my_stmt", {});
    if (!check_success(co_await conn.exec(req_prepare, check(), &diag), diag))
        co_return;

    // Close it
    request req_close;
    req_close.add_close_statement("my_stmt");
    if (!check_success(co_await conn.exec(req_close, check(), &diag), diag))
        co_return;

    // The statement can no longer be used
    request req_execute;
    req_execute.add_execute("my_stmt", {});
    auto [ec] = co_await conn.exec(req_execute, check(), &diag);
    BOOST_TEST_EQ(ec, sqlstate_cond::cannot_connect_now);
}

}  // namespace

int main()
{
    run_coroutine_test(test_static_params());
    run_coroutine_test(test_dynamic_params_without_oids());
    run_coroutine_test(test_dynamic_params_with_oids());
    run_coroutine_test(test_no_params());

    return boost::report_errors();
}