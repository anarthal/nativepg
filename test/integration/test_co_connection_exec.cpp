//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/assert/source_location.hpp>
#include <boost/capy/cond.hpp>
#include <boost/capy/error.hpp>
#include <boost/capy/ex/async_event.hpp>
#include <boost/capy/ex/immediate.hpp>
#include <boost/capy/ex/run.hpp>
#include <boost/capy/io_task.hpp>
#include <boost/capy/when_all.hpp>
#include <boost/core/lightweight_test.hpp>
#include <boost/describe/class.hpp>
#include <boost/describe/operators.hpp>

#include <stop_token>
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
#include "nativepg/responses/resultset_callback.hpp"
#include "test_utils/co_connection_utils.hpp"
#include "test_utils/corosio_utils.hpp"
#include "test_utils/printing.hpp"
#include "test_utils/test_cond_eq.hpp"
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

// Exec (potentially with pipelining) works
capy::task<> test_success()
{
    // Setup
    auto conn = co_await establish_connection();

    // Request and response
    request req;
    req.add_query("SELECT $1 + $2 AS value", 42, 10);
    req.add_query("SELECT $1 AS value", "abcd");
    std::vector<row_int> ints;
    std::vector<row_string> strings;

    // Execute
    if (!co_await checked_exec(conn, req, response{into(ints), into(strings)}))
        co_return;

    // Check
    ;
    test_range_eq(ints, std::vector<row_int>{{.value = 52}});
    test_range_eq(strings, std::vector<row_string>{{.value = "abcd"}});
}

// exec processes any GUC reported while reading the response
capy::task<> test_gucs()
{
    // Setup
    auto conn = co_await establish_connection();

    // Change a GUC
    request req;
    req.add_simple_query("SET client_encoding TO 'LATIN1'");
    if (!co_await checked_exec(conn, req))
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
    auto conn = co_await establish_connection();

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
    auto conn = co_await establish_connection();

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
    auto conn = co_await establish_connection();

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
// Cancellation. The key point here is verifying that we leave the connection
// in a usable state, whatever we do.
//

// A request cancelled while its write or its read is still pending
capy::task<> test_cancel_single()
{
    // Setup
    auto conn = co_await establish_connection();

    request req;
    req.add_query("SELECT $1 AS value", 42);

    // Run a request that gets cancelled immediately
    static_cast<void>(co_await capy::when_all(
        [&]() -> capy::io_task<> {
            // This request will be cancelled
            auto [ec] = co_await conn.exec(req, check());
            test_cond_eq(ec, capy::cond::canceled);
            co_return {};
        }(),
        capy::ready(std::make_error_code(std::errc::io_error))
    ));

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
    auto conn = co_await establish_connection(), conn_lock = co_await establish_connection();

    // Acquire the lock. Note: different tests should use different IDs
    request req_lock;
    req_lock.add_query("SELECT pg_advisory_lock($1)", 1);
    if (!co_await checked_exec(conn_lock, req_lock))
        co_return;

    request req;
    req.add_query("SELECT $1 AS value", 42);
    req.add_query("SELECT pg_advisory_lock($1)", 1);
    req.add_query("SELECT $1 AS value", 50);

    capy::async_event select_finished;

    static_cast<void>(co_await capy::when_all(
        [&]() -> capy::io_task<> {
            // Run the request. It will get cancelled after the SELECT finishes
            row_int row{};
            auto cb = [&](row_int r) {
                row = r;
                select_finished.set();
            };
            auto [ec] = co_await conn.exec(
                req,
                response{resultset_callback<row_int>(cb), check_execute(), check_execute()}
            );
            test_cond_eq(ec, capy::cond::canceled);
            BOOST_TEST_EQ(row, row_int{.value = 42});
            co_return {};
        }(),
        [&]() -> capy::io_task<> {
            // Trigger a cancellation once the first SELECT receives its data
            auto [ec] = co_await select_finished.wait();
            BOOST_TEST_EQ(ec, std::error_code());
            co_return std::make_error_code(std::errc::address_in_use);
        }()
    ));

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
    auto conn = co_await establish_connection();

    request req1;
    req1.add_query("SELECT $1 AS value", 42);

    request req2;
    req2.add_query("SELECT $1 AS value", "abcd");

    std::stop_source stop_src;

    // Recall that when_all launches things in order
    static_cast<void>(co_await capy::when_all(
        capy::run(stop_src.get_token())([&]() -> capy::io_task<> {
            // Run req1, which will get cancelled
            auto [ec] = co_await conn.exec(req1, check());
            test_cond_eq(ec, capy::cond::canceled);
            co_return {};
        }()),

        [&]() -> capy::io_task<> {
            // Run req2, which should complete normally
            std::vector<row_string> strings2;
            if (co_await checked_exec(conn, req2, into(strings2)))
                test_range_eq(strings2, std::vector<row_string>{{.value = "abcd"}});
            co_return {};
        }(),

        // Cancel req1 immediately
        [&]() -> capy::io_task<> {
            stop_src.request_stop();
            co_return {};
        }()
    ));

    co_await check_connection_usable(conn);
}

// Same, but with a second request queued behind the cancelled one. The queued
// request has to skip the ReadyForQuery messages still owed for its abandoned
// predecessor before it can read its own response.
capy::task<> test_cancel_partial_response_with_queued()
{
    // Setup
    auto conn = co_await establish_connection(), conn_lock = co_await establish_connection();

    // Acquire the lock. Note: different tests should use different IDs
    request req_lock;
    req_lock.add_query("SELECT pg_advisory_lock($1)", 2);
    if (!co_await checked_exec(conn_lock, req_lock))
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
    capy::async_event req1_finished;
    std::stop_source stop_src;

    static_cast<void>(co_await capy::when_all(
        capy::run(stop_src.get_token())([&]() -> capy::io_task<> {
            // Blocks on the lock and should be cancelled
            row_int row{};
            auto cb = [&](row_int r) {
                row = r;
                select_finished.set();
            };
            auto [ec] = co_await conn.exec(req1, response{resultset_callback<row_int>(cb), check_execute()});
            test_cond_eq(ec, capy::cond::canceled);
            BOOST_TEST_EQ(row, row_int{.value = 42});

            // Notify downstream tasks
            req1_finished.set();
            co_return {};
        }()),
        [&]() -> capy::io_task<> {
            // Just runs req2, which should succeed
            if (co_await checked_exec(conn, req2, into(strings2)))
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

// A request cancelled while waiting for its turn to read
capy::task<> test_cancel_while_waiting()
{
    // The first request blocks on the lock. The 2nd one
    // writes but is cancelled while waiting for its turn to read.

    // Setup
    auto conn = co_await establish_connection(), conn_lock = co_await establish_connection();

    // Acquire the lock. Note: different tests should use different IDs
    request req_lock;
    req_lock.add_query("SELECT pg_advisory_lock($1)", 3);
    if (!co_await checked_exec(conn_lock, req_lock))
        co_return;

    request req1;
    req1.add_query("SELECT $1 AS value", 42);
    req1.add_query("SELECT pg_advisory_lock($1)", 3);

    request req2;
    req2.add_query("SELECT $1 AS value", "abcd");

    capy::async_event select_finished;
    capy::async_event req2_finished;
    std::stop_source stop_src;

    // Recall that when_all launches things in order
    static_cast<void>(co_await capy::when_all(
        [&]() -> capy::io_task<> {
            // Blocks on the lock, and should succeed once it is released
            row_int row{};
            auto cb = [&](row_int r) {
                row = r;
                select_finished.set();
            };
            const bool ok = co_await checked_exec(
                conn,
                req1,
                response{resultset_callback<row_int>(cb), check_execute()}
            );
            if (ok)
                BOOST_TEST_EQ(row, row_int{.value = 42});
            co_return {};
        }(),
        capy::run(stop_src.get_token())([&]() -> capy::io_task<> {
            // Never gets its turn to read, and should be cancelled there
            std::vector<row_string> strings2;
            auto [ec] = co_await conn.exec(req2, into(strings2));
            test_cond_eq(ec, capy::cond::canceled);

            // Notify downstream tasks
            req2_finished.set();
            co_return {};
        }()),
        [&]() -> capy::io_task<> {
            // Wait for the SELECT to finish, then cancels req2
            auto [ec] = co_await select_finished.wait();
            BOOST_TEST_EQ(ec, std::error_code());
            stop_src.request_stop();
            co_return {};
        }(),
        [&]() -> capy::io_task<> {
            // Waits for req2 to be finish, then releases the lock so req1 can make progress.
            auto [ec] = co_await req2_finished.wait();
            BOOST_TEST_EQ(ec, std::error_code());
            check_success(co_await conn_lock.shutdown(), {});
            co_return {};
        }()
    ));

    co_await check_connection_usable(conn);
}

// Same, with three requests, cancelling the middle one. The third request has
// to skip the response of the cancelled one.
capy::task<> test_cancel_while_waiting_middle()
{
    // Setup
    auto conn = co_await establish_connection(), conn_lock = co_await establish_connection();

    // Acquire the lock. Note: different tests should use different IDs
    request req_lock;
    req_lock.add_query("SELECT pg_advisory_lock($1)", 4);
    if (!co_await checked_exec(conn_lock, req_lock))
        co_return;

    // Holds the reader while blocked on the lock, and should succeed
    request req1;
    req1.add_query("SELECT $1 AS value", 42);
    req1.add_query("SELECT pg_advisory_lock($1)", 4);

    // The middle one, cancelled while waiting for its turn to read
    request req2;
    req2.add_query("SELECT $1 AS value", "middle");

    // Queued behind the cancelled one, should succeed
    request req3;
    req3.add_query("SELECT $1 AS value", "third");

    // Response
    capy::async_event select_finished;
    capy::async_event req2_finished;
    std::stop_source stop_src;

    // Recall that when_all launches things in order
    static_cast<void>(co_await capy::when_all(
        [&]() -> capy::io_task<> {
            // Blocks on the lock, and should succeed once it is released
            row_int row{};
            auto cb = [&](row_int r) {
                row = r;
                select_finished.set();
            };
            const bool ok = co_await checked_exec(
                conn,
                req1,
                response{resultset_callback<row_int>(cb), check_execute()}
            );
            if (ok)
                BOOST_TEST_EQ(row, row_int{.value = 42});
            co_return {};
        }(),
        capy::run(stop_src.get_token())([&]() -> capy::io_task<> {
            // Never gets its turn to read, and should be cancelled there
            std::vector<row_string> strings2;
            auto [ec] = co_await conn.exec(req2, into(strings2));
            test_cond_eq(ec, capy::cond::canceled);

            // Notify downstream tasks
            req2_finished.set();
            co_return {};
        }()),
        [&]() -> capy::io_task<> {
            // Reads past the cancelled request's leftovers, and should succeed
            std::vector<row_string> strings3;
            if (co_await checked_exec(conn, req3, into(strings3)))
                test_range_eq(strings3, std::vector<row_string>{{.value = "third"}});
            co_return {};
        }(),
        [&]() -> capy::io_task<> {
            // Wait for the SELECT to finish, then cancels req2
            auto [ec] = co_await select_finished.wait();
            BOOST_TEST_EQ(ec, std::error_code());
            stop_src.request_stop();
            co_return {};
        }(),
        [&]() -> capy::io_task<> {
            // Waits for req2 to be gone, then releases the lock so req1 and req3
            // can make progress. Releasing earlier would race req2's cancellation
            auto [ec] = co_await req2_finished.wait();
            BOOST_TEST_EQ(ec, std::error_code());
            check_success(co_await conn_lock.shutdown(), {});
            co_return {};
        }()
    ));

    co_await check_connection_usable(conn);
}

}  // namespace

int main()
{
    run_coroutine_test(test_success());
    run_coroutine_test(test_gucs());
    run_coroutine_test(test_multiplexing_2());
    run_coroutine_test(test_multiplexing_3());
    run_coroutine_test(test_handler_error());
    run_coroutine_test(test_cancel_single());
    run_coroutine_test(test_cancel_partial_response());
    run_coroutine_test(test_cancel_single_with_queued());
    run_coroutine_test(test_cancel_partial_response_with_queued());
    run_coroutine_test(test_cancel_while_waiting());
    run_coroutine_test(test_cancel_while_waiting_middle());

    return boost::report_errors();
}
