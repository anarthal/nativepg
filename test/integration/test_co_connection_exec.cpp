//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/assert/source_location.hpp>
#include <boost/capy/cond.hpp>
#include <boost/capy/delay.hpp>
#include <boost/capy/error.hpp>
#include <boost/capy/ex/async_event.hpp>
#include <boost/capy/ex/immediate.hpp>
#include <boost/capy/ex/run.hpp>
#include <boost/capy/ex/run_async.hpp>
#include <boost/capy/ex/this_coro.hpp>
#include <boost/capy/io_result.hpp>
#include <boost/capy/io_task.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/timeout.hpp>
#include <boost/capy/when_all.hpp>
#include <boost/capy/when_any.hpp>
#include <boost/core/lightweight_test.hpp>
#include <boost/corosio/io_context.hpp>
#include <boost/describe/class.hpp>
#include <boost/describe/operators.hpp>

#include <chrono>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "nativepg/co_connection.hpp"
#include "nativepg/encoding.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/request.hpp"
#include "nativepg/responses/check.hpp"
#include "nativepg/responses/into.hpp"
#include "nativepg/responses/response.hpp"
#include "nativepg/responses/response_handler.hpp"
#include "nativepg/responses/resultset_callback.hpp"
#include "test_utils/ci_server.hpp"
#include "test_utils/corosio_utils.hpp"
#include "test_utils/printing.hpp"
#include "test_utils/test_opt_eq.hpp"
#include "test_utils/test_range_eq.hpp"

namespace capy = boost::capy;
using namespace nativepg;
using namespace nativepg::test;
using namespace std::string_view_literals;
using namespace std::chrono_literals;

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

// Runs a plain request and checks it produces its own response (detects de-syncs)
capy::task<> check_connection_usable(co_connection& conn, boost::source_location loc = BOOST_CURRENT_LOCATION)
{
    request req;
    req.add_query("SELECT $1 AS value", 1234);
    std::vector<row_int> ints;
    diagnostics diag;
    if (check_success(co_await conn.exec(req, into(ints), &diag), diag, loc))
        test_range_eq(ints, std::vector<row_int>{{.value = 1234}}, loc);
}

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

    // A handler error is not a protocol error
    co_await check_connection_usable(conn);
}

//
// Cancellation
//
// All of these check the same underlying property: whatever the cancelled
// request had already put on the wire, the connection is left in sync, so the
// requests that follow it still read their own responses. The multiplexer does
// this by counting the ReadyForQuery messages the server still owes for the
// abandoned request and skipping them.
//

// A request cancelled while its write or its read is still pending
capy::task<> test_cancel_single()
{
    // Setup
    diagnostics diag;
    co_connection conn{co_await capy::this_coro::executor};
    if (!check_success(co_await conn.connect(default_connect_params(), &diag), diag))
        co_return;

    request req;
    req.add_query("SELECT $1 AS value", 42);

    // Run a request that gets cancelled immediately
    auto [dummy, res, dummy2] = co_await capy::when_all(
        do_exec(conn, req, check()),
        capy::ready(std::make_error_code(std::errc::io_error))
    );

    // Check
    BOOST_TEST(res.code == capy::cond::canceled);
    co_await check_connection_usable(conn);
}

// A request cancelled when part of its response has already been read
capy::task<> test_cancel_partial_response()
{
    // The main request acquires an advisory lock that we ensure is
    // already locked, causing locking. We use this to ensure that
    // the cancellation arrives with part of the response read.
    // We avoid pg_sleep because it causes brittle tests and increases runtime.

    // Setup
    diagnostics diag;
    co_connection conn{co_await capy::this_coro::executor}, conn_lock{co_await capy::this_coro::executor};
    if (!check_success(co_await conn.connect(default_connect_params(), &diag), diag) ||
        !check_success(co_await conn_lock.connect(default_connect_params(), &diag), diag))
        co_return;

    // Acquire the lock. Note: different tests should use different IDs
    request req_lock;
    req_lock.add_query("SELECT pg_advisory_lock($1)", 1);
    if (!check_success(co_await conn_lock.exec(req_lock, check(), &diag), diag))
        co_return;

    // The cancellation should arrive while we wait for the lock
    request req;
    req.add_query("SELECT $1 AS value", 42);
    req.add_query("SELECT pg_advisory_lock($1)", 1);
    req.add_query("SELECT $1 AS value", 50);

    // Response
    capy::async_event select_finished;
    row_int row{};
    auto cb = [&](row_int r) {
        row = r;
        select_finished.set();
    };

    auto [dummy, res, dummy2] = co_await capy::when_all(
        // Run the request that will block indefinitely
        do_exec(conn, req, response{resultset_callback<row_int>(cb), check_execute(), check_execute()}),

        // Trigger a cancellation once the first SELECT receives its data
        [&]() -> capy::io_task<> {
            auto [ec] = co_await select_finished.wait();
            BOOST_TEST_EQ(ec, std::error_code());
            co_return std::make_error_code(std::errc::address_in_use);
        }()
    );

    // Check. We should have received the SELECT's data
    BOOST_TEST(res.code == capy::cond::canceled);
    BOOST_TEST_EQ(row, row_int{.value = 42});

    // Release the lock so the request completes server-side
    if (!check_success(co_await conn_lock.shutdown(), {}))
        co_return;

    // We read the leftovers and leave the connection usable
    co_await check_connection_usable(conn);
}

// A request cancelled while its write or read is pending, with a second
// request queued behind it. The second one may have to finish writing the
// first one's payload, and to skip whatever response it produced.
capy::task<> test_cancel_single_with_queued()
{
    // Setup
    diagnostics diag;
    co_connection conn{co_await capy::this_coro::executor};
    if (!check_success(co_await conn.connect(default_connect_params(), &diag), diag))
        co_return;

    request req1;
    req1.add_query("SELECT $1 AS value", 42);
    std::vector<row_int> ints1;

    request req2;
    req2.add_query("SELECT $1 AS value", "abcd");
    std::vector<row_string> strings2;

    // Recall that when_all launches things in order
    auto [dummy, res1, res2, dummy2] = co_await capy::when_all(
        // The first query will be cancelled
        do_exec(conn, req1, into(ints1)),

        // The second one won't because we're binding it to an empty stop token
        capy::run(std::stop_token())(do_exec(conn, req2, into(strings2))),

        // Cancel things immediately
        capy::ready(std::make_error_code(std::errc::io_error))
    );

    // Check
    BOOST_TEST(res1.code == capy::cond::canceled);
    check_success(res2);
    test_range_eq(strings2, std::vector<row_string>{{.value = "abcd"}});

    co_await check_connection_usable(conn);
}

// Same, but with a second request queued behind the cancelled one. The queued
// request has to skip the ReadyForQuery messages still owed for its abandoned
// predecessor before it can read its own response.
capy::task<> test_cancel_partial_response_with_queued()
{
    // Setup
    diagnostics diag;
    co_connection conn{co_await capy::this_coro::executor}, conn_lock{co_await capy::this_coro::executor};
    if (!check_success(co_await conn.connect(default_connect_params(), &diag), diag) ||
        !check_success(co_await conn_lock.connect(default_connect_params(), &diag), diag))
        co_return;

    // Acquire the lock. Note: different tests should use different IDs
    request req_lock;
    req_lock.add_query("SELECT pg_advisory_lock($1)", 2);
    if (!check_success(co_await conn_lock.exec(req_lock, check(), &diag), diag))
        co_return;

    // The cancellation arrives while this one waits for the lock, with the
    // first query's response already read
    request req1;
    req1.add_query("SELECT $1 AS value", 42);
    req1.add_query("SELECT pg_advisory_lock($1)", 2);

    // Queued behind req1, and should succeed
    request req2;
    req2.add_query("SELECT $1 AS value", "abcd");
    std::vector<row_string> strings2;

    // Response
    capy::async_event select_finished;
    row_int row{};
    auto cb = [&](row_int r) {
        row = r;
        select_finished.set();
    };
    capy::async_event req1_finished;
    std::stop_source stop_src;

    static_cast<void>(co_await capy::when_all(
        capy::run(stop_src.get_token())([&]() -> capy::io_task<> {
            // Blocks on the lock and should be cancelled
            auto [ec] = co_await conn.exec(req1, response{resultset_callback<row_int>(cb), check_execute()});
            BOOST_TEST(ec == capy::cond::canceled);

            // Notify downstream tasks
            req1_finished.set();
            co_return {};
        }()),
        [&]() -> capy::io_task<> {
            // Just runs req2, which should succeed
            auto [dummy, err] = co_await do_exec(conn, req2, into(strings2));
            check_success(err);
            test_range_eq(strings2, std::vector<row_string>{{.value = "abcd"}});
            co_return {};
        }(),
        [&]() -> capy::io_task<> {
            // Wait for the SELECT to finish, then cancels req1
            auto [ec] = co_await select_finished.wait();
            BOOST_TEST_EQ(ec, std::error_code());
            stop_src.request_stop();
            co_return {};
        }(),
        [&]() -> capy::io_task<> {
            // Waits for req1 to finish, then releases the lock so req2 can make progress
            auto [ec] = co_await req1_finished.wait();
            BOOST_TEST_EQ(ec, std::error_code());
            check_success(co_await conn_lock.shutdown(), {});
            co_return {};
        }()
    ));

    co_await check_connection_usable(conn);
}

// A request cancelled while waiting for its turn to read. The first request
// blocks on the lock, so the second one gets its payload written but then sits
// in the multiplexer's queue, which is where the cancellation finds it.
capy::task<> test_cancel_while_waiting()
{
    // Setup
    diagnostics diag;
    co_connection conn{co_await capy::this_coro::executor}, conn_lock{co_await capy::this_coro::executor};
    if (!check_success(co_await conn.connect(default_connect_params(), &diag), diag) ||
        !check_success(co_await conn_lock.connect(default_connect_params(), &diag), diag))
        co_return;

    // Acquire the lock. Note: different tests should use different IDs
    request req_lock;
    req_lock.add_query("SELECT pg_advisory_lock($1)", 3);
    if (!check_success(co_await conn_lock.exec(req_lock, check(), &diag), diag))
        co_return;

    // Holds the reader while blocked on the lock. Its first query answering is
    // what tells us that req2 has entered the multiplexer and is waiting.
    request req1;
    req1.add_query("SELECT $1 AS value", 42);
    req1.add_query("SELECT pg_advisory_lock($1)", 3);

    // Written, then queued waiting for its turn to read. This is the one we cancel.
    request req2;
    req2.add_query("SELECT $1 AS value", "abcd");
    std::vector<row_string> strings2;

    // Response
    capy::async_event select_finished;
    row_int row{};
    auto cb = [&](row_int r) {
        row = r;
        select_finished.set();
    };

    // Recall that when_all launches things in order
    auto [dummy, res1, res2, dummy2, dummy3] = co_await capy::when_all(
        // Immune to the cancellation: this one must complete
        capy::run(std::stop_token())(
            do_exec(conn, req1, response{resultset_callback<row_int>(cb), check_execute()})
        ),

        // Cancelled while waiting for its turn to read
        do_exec(conn, req2, into(strings2)),

        // Trigger the cancellation once the first SELECT receives its data
        [&]() -> capy::io_task<> {
            auto [ec] = co_await select_finished.wait();
            BOOST_TEST_EQ(ec, std::error_code());
            co_return std::make_error_code(std::errc::address_in_use);
        }(),

        // Release the lock once the cancellation has been raised, so req1 can
        // finish. This waits on the same event as the canceller: set() wakes
        // waiters in registration order and when_all launches in argument order,
        // so the canceller resumes first, and the error it returns makes the stop
        // request before this task is resumed. Releasing the lock any earlier
        // could let req1 complete instead of being cancelled.
        // Immune to cancellation, or the shutdown would be cancelled too.
        capy::run(std::stop_token())([&]() -> capy::io_task<> {
            static_cast<void>(co_await select_finished.wait());
            static_cast<void>(co_await conn_lock.shutdown());
            co_return {};
        }())
    );

    // Check. req1 is unaffected; req2 never gets to read, so its response is
    // left for whoever comes next to skip.
    check_success(res1);
    BOOST_TEST_EQ(row, row_int{.value = 42});
    BOOST_TEST(res2.code == capy::cond::canceled);

    co_await check_connection_usable(conn);
}

// Same, with three requests, cancelling the middle one. The third request has
// to skip the response of the cancelled one, which is handed over to it rather
// than left to the connection's trailing count.
capy::task<> test_cancel_while_waiting_middle()
{
    // Setup
    diagnostics diag;
    co_connection conn{co_await capy::this_coro::executor}, conn_lock{co_await capy::this_coro::executor};
    if (!check_success(co_await conn.connect(default_connect_params(), &diag), diag) ||
        !check_success(co_await conn_lock.connect(default_connect_params(), &diag), diag))
        co_return;

    // Acquire the lock. Note: different tests should use different IDs
    request req_lock;
    req_lock.add_query("SELECT pg_advisory_lock($1)", 4);
    if (!check_success(co_await conn_lock.exec(req_lock, check(), &diag), diag))
        co_return;

    // Holds the reader while blocked on the lock
    request req1;
    req1.add_query("SELECT $1 AS value", 42);
    req1.add_query("SELECT pg_advisory_lock($1)", 4);

    // The middle one, cancelled while waiting for its turn to read
    request req2;
    req2.add_query("SELECT $1 AS value", "middle");
    std::vector<row_string> strings2;

    // Queued behind the cancelled one, so it inherits its leftovers
    request req3;
    req3.add_query("SELECT $1 AS value", "third");
    std::vector<row_string> strings3;

    // Response
    capy::async_event select_finished;
    row_int row{};
    auto cb = [&](row_int r) {
        row = r;
        select_finished.set();
    };

    // Recall that when_all launches things in order
    auto [dummy, res1, res2, res3, dummy2, dummy3] = co_await capy::when_all(
        // Immune to the cancellation: this one must complete
        capy::run(std::stop_token())(
            do_exec(conn, req1, response{resultset_callback<row_int>(cb), check_execute()})
        ),

        // Cancelled while waiting for its turn to read
        do_exec(conn, req2, into(strings2)),

        // Immune, too: it must read past req2's leftovers and find its own response
        capy::run(std::stop_token())(do_exec(conn, req3, into(strings3))),

        // Trigger the cancellation once the first SELECT receives its data
        [&]() -> capy::io_task<> {
            auto [ec] = co_await select_finished.wait();
            BOOST_TEST_EQ(ec, std::error_code());
            co_return std::make_error_code(std::errc::address_in_use);
        }(),

        // Release the lock once the cancellation has been raised, so req1 and req3 can
        // finish. This waits on the same event as the canceller: set() wakes
        // waiters in registration order and when_all launches in argument order,
        // so the canceller resumes first, and the error it returns makes the stop
        // request before this task is resumed. Releasing the lock any earlier
        // could let req1 complete instead of being cancelled.
        // Immune to cancellation, or the shutdown would be cancelled too.
        capy::run(std::stop_token())([&]() -> capy::io_task<> {
            static_cast<void>(co_await select_finished.wait());
            static_cast<void>(co_await conn_lock.shutdown());
            co_return {};
        }())
    );

    // Check
    check_success(res1);
    BOOST_TEST_EQ(row, row_int{.value = 42});
    BOOST_TEST(res2.code == capy::cond::canceled);
    if (check_success(res3))
        test_range_eq(strings3, std::vector<row_string>{{.value = "third"}});

    co_await check_connection_usable(conn);
}

}  // namespace

volatile int myint = 120;

int main()
{
    if (myint < 10)
    {
        run_coroutine_test(test_success());
        run_coroutine_test(test_gucs());
        run_coroutine_test(test_multiplexing_2());
        run_coroutine_test(test_multiplexing_3());
        run_coroutine_test(test_handler_error());
        run_coroutine_test(test_cancel_single());
        run_coroutine_test(test_cancel_partial_response());
        run_coroutine_test(test_cancel_single_with_queued());
    }
    run_coroutine_test(test_cancel_partial_response_with_queued());

    if (myint < 10)
    {
        run_coroutine_test(test_cancel_while_waiting());
        run_coroutine_test(test_cancel_while_waiting_middle());
    }

    return boost::report_errors();
}
