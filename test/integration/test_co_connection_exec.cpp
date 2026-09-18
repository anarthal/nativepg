//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/assert/source_location.hpp>
#include <boost/capy/ex/run_async.hpp>
#include <boost/capy/ex/this_coro.hpp>
#include <boost/capy/io_result.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/when_all.hpp>
#include <boost/core/lightweight_test.hpp>
#include <boost/corosio/io_context.hpp>
#include <boost/describe/class.hpp>
#include <boost/describe/operators.hpp>

#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "nativepg/co_connection.hpp"
#include "nativepg/encoding.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/request.hpp"
#include "nativepg/responses/check.hpp"
#include "nativepg/responses/into.hpp"
#include "nativepg/responses/response.hpp"
#include "nativepg/responses/response_handler.hpp"
#include "test_utils/ci_server.hpp"
#include "test_utils/corosio_utils.hpp"
#include "test_utils/printing.hpp"
#include "test_utils/test_opt_eq.hpp"
#include "test_utils/test_range_eq.hpp"

namespace capy = boost::capy;
using namespace nativepg;
using namespace nativepg::test;
using namespace std::string_view_literals;

namespace {

struct row_int
{
    int value;
};
BOOST_DESCRIBE_STRUCT(row_int, (), (value))

struct row_string
{
    std::string value;
};
BOOST_DESCRIBE_STRUCT(row_string, (), (value))

using boost::describe::operators::operator==;
using boost::describe::operators::operator<<;

// Exec (potentially with pipelining) works
capy::task<> test_success()
{
    // Setup
    diagnostics diag;
    co_connection conn{co_await capy::this_coro::executor};
    if (!check_success(co_await conn.connect(default_connect_params(), &diag), diag))
        co_return;

    // Request and response
    request req;
    req.add_query("SELECT $1 + $2 AS value", 42, 10);
    req.add_query("SELECT $1 AS value", "abcd");
    std::vector<row_int> ints;
    std::vector<row_string> strings;

    // Execute
    if (!check_success(co_await conn.exec(req, response{into(ints), into(strings)}, &diag), diag))
        co_return;

    // Check
    std::vector<row_int> ints_expected{{.value = 52}};
    std::vector<row_string> strings_expected{{.value = "abcd"}};
    BOOST_TEST_ALL_EQ(ints.begin(), ints.end(), ints_expected.begin(), ints_expected.end());
    BOOST_TEST_ALL_EQ(strings.begin(), strings.end(), strings_expected.begin(), strings_expected.end());
}

// exec processes any GUC reported while reading the response
capy::task<> test_gucs()
{
    // Setup
    diagnostics diag;
    co_connection conn{co_await capy::this_coro::executor};
    if (!check_success(co_await conn.connect(default_connect_params(), &diag), diag))
        co_return;

    // Change a GUC
    request req;
    req.add_simple_query("SET client_encoding TO 'LATIN1'");
    if (!check_success(co_await conn.exec(req, check(), &diag), diag))
        co_return;

    // Check
    test_opt_eq(conn.client_encoding(), encoding::latin1);
}

// Runs a single exec and records its outcome, always completing successfully.
// Prevents when_all from cancelling siblings on error.
template <response_handler Handler>
capy::io_task<extended_error> do_exec(co_connection& conn, const request& req, Handler handler)
{
    extended_error out;
    auto [ec] = co_await conn.exec(req, &handler, &out.diag);
    out.code = ec;
    co_return {{}, out};
}

// Two requests issued in parallel over a single connection complete, and each
// handler sees only its own response
capy::task<> test_multiplexing_2()
{
    // Setup
    diagnostics diag;
    co_connection conn{co_await capy::this_coro::executor};
    if (!check_success(co_await conn.connect(default_connect_params(), &diag), diag))
        co_return;

    // Two requests with distinct result types, so a misrouted message can't
    // pass unnoticed
    request req1;
    req1.add_query("SELECT $1 + $2 AS value", 42, 10);
    std::vector<row_int> ints;

    request req2;
    req2.add_query("SELECT $1 AS value", "abcd");
    std::vector<row_string> strings;

    // Both requests are handed to the multiplexer before either completes
    auto [dummy, res1, res2] = co_await capy::when_all(
        do_exec(conn, req1, into(ints)),
        do_exec(conn, req2, into(strings))
    );

    // Check
    if (!check_success(res1) || !check_success(res2))
        co_return;
    test_range_eq(ints, std::vector<row_int>{{.value = 52}});
    test_range_eq(strings, std::vector<row_string>{{.value = "abcd"}});
}

// Same, with three requests
capy::task<> test_multiplexing_3()
{
    // Setup
    diagnostics diag;
    co_connection conn{co_await capy::this_coro::executor};
    if (!check_success(co_await conn.connect(default_connect_params(), &diag), diag))
        co_return;

    request req1;
    req1.add_query("SELECT $1 + $2 AS value", 42, 10);
    std::vector<row_int> ints1;

    // Two queries in a single request
    request req2;
    req2.add_query("SELECT $1 AS value", 7);
    req2.add_query("SELECT $1 AS value", "hello");
    std::vector<row_int> ints2;
    std::vector<row_string> strings2;

    request req3;
    req3.add_query("SELECT $1 AS value", "world!");
    std::vector<row_string> strings3;

    // All three requests are handed to the multiplexer before any completes
    auto [dummy, res1, res2, res3] = co_await capy::when_all(
        do_exec(conn, req1, into(ints1)),
        do_exec(conn, req2, response{into(ints2), into(strings2)}),
        do_exec(conn, req3, into(strings3))
    );

    // Check
    if (!check_success(res1) || !check_success(res2) || !check_success(res3))
        co_return;

    test_range_eq(ints1, std::vector<row_int>{{.value = 52}});
    test_range_eq(ints2, std::vector<row_int>{{.value = 7}});
    test_range_eq(strings2, std::vector<row_string>{{.value = "hello"}});
    test_range_eq(strings3, std::vector<row_string>{{.value = "world!"}});
}

// exec() reports the error code and the diagnostics produced by the handler
capy::task<> test_handler_error()
{
    // A response handler that reports a well-known error
    struct failing_handler
    {
        handler_setup_result setup(const request& req, std::size_t) { return req.messages().size(); }

        void on_message(const any_request_message&, std::size_t, extended_error& err)
        {
            err.code = std::make_error_code(std::errc::invalid_argument);
            err.diag = diagnostics("some_error");
        }
    };

    // Setup
    diagnostics diag;
    co_connection conn{co_await capy::this_coro::executor};
    if (!check_success(co_await conn.connect(default_connect_params(), &diag), diag))
        co_return;

    // The query itself is valid: the error comes from the handler
    request req;
    req.add_query("SELECT $1 AS value", 42);
    diagnostics exec_diag;
    auto [ec] = co_await conn.exec(req, failing_handler(), &exec_diag);

    // Check
    BOOST_TEST_EQ(ec, std::make_error_code(std::errc::invalid_argument));
    BOOST_TEST_EQ(exec_diag.message(), "some_error"sv);

    // A handler error is not a protocol error: the response was read in full,
    // so the connection is still usable
    check_success(co_await conn.exec(req, check(), &diag), diag);
}

}  // namespace

int main()
{
    run_coroutine_test(test_success());
    run_coroutine_test(test_gucs());
    run_coroutine_test(test_multiplexing_2());
    run_coroutine_test(test_multiplexing_3());
    run_coroutine_test(test_handler_error());

    return boost::report_errors();
}
