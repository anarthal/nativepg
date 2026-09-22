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

// // Long enough for a cancellation to land while the query is still running,
// // short enough to stay well inside the test timeout
// constexpr std::string_view sleep_query = "SELECT pg_sleep(1)";
// constexpr std::chrono::milliseconds cancel_delay{200};

// // Same as do_exec, but runs the exec under its own stop token, so it can be
// // cancelled without affecting its siblings
// template <response_handler Handler>
// capy::io_task<extended_error> do_exec_cancellable(
//     co_connection& conn,
//     const request& req,
//     Handler handler,
//     std::stop_token token
// )
// {
//     extended_error out;
//     auto [ec] = co_await capy::run(std::move(token))(conn.exec(req, &handler, &out.diag));
//     out.code = ec;
//     co_return {{}, out};
// }

// // Requests a stop as soon as it runs. when_all posts its children in argument
// // order, so listing this after the requests it should affect means they have
// // all been issued, and have suspended on I/O, by the time it runs. None of them
// // can have completed: that would require a round trip.
// capy::io_task<> cancel_now(std::stop_source& src)
// {
//     src.request_stop();
//     co_return {};
// }

// // Requests a stop after a delay, so that the cancellation lands on a request
// // that has already consumed part of its response
// capy::io_task<> cancel_after(std::stop_source& src, std::chrono::milliseconds dur)
// {
//     static_cast<void>(co_await capy::delay(dur));
//     src.request_stop();
//     co_return {};
// }

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

// // A request cancelled while its write or read is pending, with a second
// // request queued behind it. The second one may have to finish writing the
// // first one's payload, and to skip whatever response it produced.
// capy::task<> test_cancel_single_with_queued()
// {
//     // Setup
//     diagnostics diag;
//     co_connection conn{co_await capy::this_coro::executor};
//     if (!check_success(co_await conn.connect(default_connect_params(), &diag), diag))
//         co_return;

//     request req1;
//     req1.add_query("SELECT $1 AS value", 42);
//     std::vector<row_int> ints1;

//     request req2;
//     req2.add_query("SELECT $1 AS value", "abcd");
//     std::vector<row_string> strings2;

//     std::stop_source src;

//     auto [dummy, res1, res2, dummy2] = co_await capy::when_all(
//         do_exec_cancellable(conn, req1, into(ints1), src.get_token()),
//         do_exec(conn, req2, into(strings2)),
//         cancel_now(src)
//     );

//     // Check
//     BOOST_TEST(res1.code == capy::cond::canceled);
//     if (check_success(res2))
//         test_range_eq(strings2, std::vector<row_string>{{.value = "abcd"}});

//     co_await check_connection_usable(conn);
// }

// // Same, but the cancelled request has already read part of its response, so
// // the queued one has to skip the ReadyForQuery still owed for it
// capy::task<> test_cancel_partial_response_with_queued()
// {
//     // Setup
//     diagnostics diag;
//     co_connection conn{co_await capy::this_coro::executor};
//     if (!check_success(co_await conn.connect(default_connect_params(), &diag), diag))
//         co_return;

//     request req1;
//     req1.add_query("SELECT $1 AS value", 42);
//     req1.add_query(sleep_query);
//     std::vector<row_int> ints1;

//     request req2;
//     req2.add_query("SELECT $1 AS value", "abcd");
//     std::vector<row_string> strings2;

//     std::stop_source src;

//     auto [dummy, res1, res2, dummy2] = co_await capy::when_all(
//         do_exec_cancellable(conn, req1, response{into(ints1), check_execute()}, src.get_token()),
//         do_exec(conn, req2, into(strings2)),
//         cancel_after(src, cancel_delay)
//     );

//     // Check
//     BOOST_TEST(res1.code == capy::cond::canceled);
//     test_range_eq(ints1, std::vector<row_int>{{.value = 42}});
//     if (check_success(res2))
//         test_range_eq(strings2, std::vector<row_string>{{.value = "abcd"}});

//     co_await check_connection_usable(conn);
// }

// // A request cancelled while waiting for its turn to read. The first request
// // sleeps, so the second one gets its payload written but then sits in the
// // multiplexer's queue, which is where the cancellation finds it.
// capy::task<> test_cancel_while_waiting()
// {
//     // Setup
//     diagnostics diag;
//     co_connection conn{co_await capy::this_coro::executor};
//     if (!check_success(co_await conn.connect(default_connect_params(), &diag), diag))
//         co_return;

//     request req1;
//     req1.add_query(sleep_query);

//     request req2;
//     req2.add_query("SELECT $1 AS value", "abcd");
//     std::vector<row_string> strings2;

//     std::stop_source src;

//     auto [dummy, res1, res2, dummy2] = co_await capy::when_all(
//         do_exec(conn, req1, check_execute()),
//         do_exec_cancellable(conn, req2, into(strings2), src.get_token()),
//         cancel_after(src, cancel_delay)
//     );

//     // Check. The first request is unaffected; the second never gets to read,
//     // so its response is left for whoever comes next to skip.
//     check_success(res1);
//     BOOST_TEST(res2.code == capy::cond::canceled);

//     co_await check_connection_usable(conn);
// }

// // Same, with three requests, cancelling the middle one. The third request has
// // to skip the response of the cancelled one, which is handed over to it rather
// // than left to the connection's trailing count.
// capy::task<> test_cancel_while_waiting_middle()
// {
//     // Setup
//     diagnostics diag;
//     co_connection conn{co_await capy::this_coro::executor};
//     if (!check_success(co_await conn.connect(default_connect_params(), &diag), diag))
//         co_return;

//     request req1;
//     req1.add_query(sleep_query);

//     request req2;
//     req2.add_query("SELECT $1 AS value", "middle");
//     std::vector<row_string> strings2;

//     request req3;
//     req3.add_query("SELECT $1 AS value", "third");
//     std::vector<row_string> strings3;

//     std::stop_source src;

//     auto [dummy, res1, res2, res3, dummy2] = co_await capy::when_all(
//         do_exec(conn, req1, check_execute()),
//         do_exec_cancellable(conn, req2, into(strings2), src.get_token()),
//         do_exec(conn, req3, into(strings3)),
//         cancel_after(src, cancel_delay)
//     );

//     // Check
//     check_success(res1);
//     BOOST_TEST(res2.code == capy::cond::canceled);
//     if (check_success(res3))
//         test_range_eq(strings3, std::vector<row_string>{{.value = "third"}});

//     co_await check_connection_usable(conn);
// }

}  // namespace

int main(int argc, char**)
{
    if (argc > 20)
    {
        run_coroutine_test(test_success());
        run_coroutine_test(test_gucs());
        run_coroutine_test(test_multiplexing_2());
        run_coroutine_test(test_multiplexing_3());
        run_coroutine_test(test_handler_error());
        run_coroutine_test(test_cancel_single());
    }
    run_coroutine_test(test_cancel_partial_response());
    // run_coroutine_test(test_cancel_single_with_queued());
    // run_coroutine_test(test_cancel_partial_response_with_queued());
    // run_coroutine_test(test_cancel_while_waiting());
    // run_coroutine_test(test_cancel_while_waiting_middle());

    return boost::report_errors();
}
