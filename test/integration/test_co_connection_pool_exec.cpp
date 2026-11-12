//
// Copyright (c) 2019-2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/assert/source_location.hpp>
#include <boost/capy/cond.hpp>
#include <boost/capy/ex/async_event.hpp>
#include <boost/capy/ex/immediate.hpp>
#include <boost/capy/ex/run.hpp>
#include <boost/capy/ex/this_coro.hpp>
#include <boost/capy/results.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/when_all.hpp>
#include <boost/capy/when_any.hpp>
#include <boost/core/lightweight_test.hpp>
#include <boost/corosio/timeout.hpp>
#include <boost/describe/class.hpp>
#include <boost/describe/operators.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <set>
#include <stop_token>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "nativepg/client_errc.hpp"
#include "nativepg/co_connection.hpp"
#include "nativepg/co_connection_pool.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/protocol/connection_state.hpp"
#include "nativepg/request.hpp"
#include "nativepg/responses/check.hpp"
#include "nativepg/responses/into.hpp"
#include "nativepg/responses/response.hpp"
#include "nativepg/responses/response_handler.hpp"
#include "nativepg/responses/resultset_callback.hpp"
#include "nativepg/sqlstate_cond.hpp"
#include "test_utils/ci_server.hpp"
#include "test_utils/co_connection_utils.hpp"
#include "test_utils/corosio_utils.hpp"
#include "test_utils/printing.hpp"
#include "test_utils/test_cond_eq.hpp"
#include "test_utils/test_range_eq.hpp"
#include "test_utils/yield.hpp"

namespace capy = boost::capy;
namespace corosio = boost::corosio;
using namespace nativepg;
using namespace nativepg::test;
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

// Advisory lock IDs. These are global to the server, and tests may run in parallel
// with other test binaries, so we use a range that no other test uses
constexpr int lock_id_leftovers = 1001;

pool_params create_pool_params(std::size_t multiplexed_size = 2, std::size_t max_size = 151)
{
    return {
        .transport = default_connect_params(),
        .max_size = max_size,
        .multiplexed = {.size = multiplexed_size},
    };
}

// Params for a pool that can never establish a connection, so exec() never
// finds a healthy connection. The long retry_interval prevents the pool
// from spinning on connection attempts while the test runs.
pool_params create_unreachable_pool_params(std::size_t multiplexed_size = 1)
{
    auto res = create_pool_params(multiplexed_size);
    res.transport.password = "bad_password";
    res.retry_interval = 1h;
    return res;
}

// Utility to run a test that requires a running pool
// TODO: duplicated
capy::task<> with_pool_running(co_connection_pool& pool, capy::task<> body)
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

// Runs a single exec on the pool and records its outcome, always completing
// successfully. Prevents when_all from cancelling siblings on error.
template <response_handler Handler>
capy::task<capy::results<extended_error>> do_pool_exec(
    co_connection_pool& pool,
    const request& req,
    Handler handler
)
{
    extended_error out;
    auto [ec] = co_await pool.exec(req, &handler, &out.diag);
    out.code = ec;
    co_return {std::error_code(), out};
}

// Runs a request through the pool, failing the test if it doesn't succeed
capy::task<bool> checked_pool_exec(
    co_connection_pool& pool,
    const request& req,
    response_handler_ref handler,
    boost::source_location loc = BOOST_CURRENT_LOCATION
)
{
    diagnostics diag;
    auto [ec] = co_await pool.exec(req, handler, &diag);
    co_return check_success(ec, diag, loc);
}

// Same, taking ownership of the handler, so temporaries can be passed
template <response_handler Handler>
capy::task<bool> checked_pool_exec(
    co_connection_pool& pool,
    const request& req,
    Handler handler,
    boost::source_location loc = BOOST_CURRENT_LOCATION
)
{
    // Keep the handler alive
    co_return co_await checked_pool_exec(pool, req, response_handler_ref(&handler), loc);
}

// Runs a plain request through the pool and checks that it produces its own
// response. Detects de-syncs caused by previous requests.
capy::task<bool> check_pool_usable(
    co_connection_pool& pool,
    boost::source_location loc = BOOST_CURRENT_LOCATION
)
{
    request req;
    req.add_query("SELECT $1 AS value", 1234);
    std::vector<row_int> ints;
    if (!co_await checked_pool_exec(pool, req, into(ints), loc))
        co_return false;
    co_return test_range_eq(ints, std::vector<row_int>{{.value = 1234}}, loc);
}

// Retrieves the backend PID of the connection that the pool used to run the request.
// Returns 0 on failure
capy::task<std::int32_t> pool_backend_pid(
    co_connection_pool& pool,
    boost::source_location loc = BOOST_CURRENT_LOCATION
)
{
    request req;
    req.add_query("SELECT pg_backend_pid() AS value");
    std::vector<row_int> ints;
    if (!co_await checked_pool_exec(pool, req, into(ints), loc))
        co_return 0;
    if (!BOOST_TEST_EQ(ints.size(), static_cast<std::size_t>(1)))
        co_return 0;
    co_return ints[0].value;
}

// --- Tests ---

// A request executed through the pool produces its response
capy::task<> test_success()
{
    co_connection_pool pool{co_await capy::this_coro::executor, create_pool_params()};

    co_await with_pool_running(pool, [&]() -> capy::task<> {
        // Two queries with distinct result types, so a misrouted message can't
        // pass unnoticed
        request req;
        req.add_query("SELECT $1 + $2 AS value", 42, 10);
        req.add_query("SELECT $1 AS value", "abcd");
        std::vector<row_int> ints;
        std::vector<row_string> strings;

        if (!co_await checked_pool_exec(pool, req, response{into(ints), into(strings)}))
            co_return;

        test_range_eq(ints, std::vector<row_int>{{.value = 52}});
        test_range_eq(strings, std::vector<row_string>{{.value = "abcd"}});
    }());
}

// Requests issued before run() starts wait for a connection, rather than failing
capy::task<> test_exec_before_run()
{
    co_connection_pool pool{co_await capy::this_coro::executor, create_pool_params()};

    static_cast<void>(co_await capy::when_any(
        [&]() -> capy::task<capy::results<>> {
            // The pool isn't running yet, so this waits until a connection is established
            request req;
            req.add_query("SELECT $1 AS value", 42);
            std::vector<row_int> ints;
            if (co_await checked_pool_exec(pool, req, into(ints)))
                test_range_eq(ints, std::vector<row_int>{{.value = 42}});
            co_return {};
        }(),
        [&]() -> capy::task<capy::results<>> {
            co_await yield();  // give some margin to the task above
            auto [ec] = co_await pool.run();
            test_cond_eq(ec, capy::cond::canceled);
            co_return {};
        }()
    ));
}

// If no connection is healthy, exec() waits, rather than failing
capy::task<> test_exec_waits_for_healthy_connection()
{
    // The pool can never establish a connection
    co_connection_pool pool{co_await capy::this_coro::executor, create_unreachable_pool_params()};

    co_await with_pool_running(pool, [&]() -> capy::task<> {
        // No connection will ever become healthy, so the request waits until it times out.
        // Note that a timeout here means success: exec() waited, instead of failing
        request req;
        req.add_query("SELECT $1 AS value", 42);
        auto [ec] = co_await corosio::timeout(pool.exec(req, check()), 100ms);
        test_cond_eq(ec, capy::cond::timeout);
    }());
}

// Several requests can be run in parallel. With a single multiplexed connection,
// all of them share the same session
capy::task<> test_parallel_execs_single_connection()
{
    co_connection_pool pool{co_await capy::this_coro::executor, create_pool_params(1)};

    co_await with_pool_running(pool, [&]() -> capy::task<> {
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

        // All three requests are handed to the pool before any completes
        auto [dummy, res1, res2, res3] = co_await capy::when_all(
            do_pool_exec(pool, req1, into(ints1)),
            do_pool_exec(pool, req2, response{into(ints2), into(strings2)}),
            do_pool_exec(pool, req3, into(strings3))
        );

        // Each handler saw only its own response
        if (!check_success(res1) || !check_success(res2) || !check_success(res3))
            co_return;
        test_range_eq(ints1, std::vector<row_int>{{.value = 52}});
        test_range_eq(ints2, std::vector<row_int>{{.value = 7}});
        test_range_eq(strings2, std::vector<row_string>{{.value = "hello"}});
        test_range_eq(strings3, std::vector<row_string>{{.value = "world!"}});

        co_await check_pool_usable(pool);
    }());
}

// Same, with several multiplexed connections available
capy::task<> test_parallel_execs_several_connections()
{
    co_connection_pool pool{co_await capy::this_coro::executor, create_pool_params(4)};

    co_await with_pool_running(pool, [&]() -> capy::task<> {
        request req1;
        req1.add_query("SELECT $1 + $2 AS value", 42, 10);
        std::vector<row_int> ints1;

        request req2;
        req2.add_query("SELECT $1 AS value", "hello");
        std::vector<row_string> strings2;

        request req3;
        req3.add_query("SELECT $1 AS value", "world!");
        std::vector<row_string> strings3;

        request req4;
        req4.add_query("SELECT $1 AS value", 7);
        std::vector<row_int> ints4;

        auto [dummy, res1, res2, res3, res4] = co_await capy::when_all(
            do_pool_exec(pool, req1, into(ints1)),
            do_pool_exec(pool, req2, into(strings2)),
            do_pool_exec(pool, req3, into(strings3)),
            do_pool_exec(pool, req4, into(ints4))
        );

        if (!check_success(res1) || !check_success(res2) || !check_success(res3) || !check_success(res4))
            co_return;
        test_range_eq(ints1, std::vector<row_int>{{.value = 52}});
        test_range_eq(strings2, std::vector<row_string>{{.value = "hello"}});
        test_range_eq(strings3, std::vector<row_string>{{.value = "world!"}});
        test_range_eq(ints4, std::vector<row_int>{{.value = 7}});

        co_await check_pool_usable(pool);
    }());
}

// The pool doesn't create a connection per request: sessions are re-used,
// and never exceed the configured number of multiplexed connections
capy::task<> test_connections_reused()
{
    constexpr std::size_t num_connections = 2;
    co_connection_pool pool{co_await capy::this_coro::executor, create_pool_params(num_connections)};

    co_await with_pool_running(pool, [&]() -> capy::task<> {
        std::set<std::int32_t> pids;
        for (int i = 0; i < 6; ++i)
        {
            const auto pid = co_await pool_backend_pid(pool);
            if (!BOOST_TEST_NE(pid, 0))
                co_return;
            pids.insert(pid);
        }

        // We ran 6 requests, but the pool never used more than the configured
        // number of connections
        BOOST_TEST_LE(pids.size(), num_connections);
    }());
}

// Multiplexed connections are not shared with get_connection():
// a connection handed to the user never serves exec() requests
capy::task<> test_dedicated_and_multiplexed_disjoint()
{
    co_connection_pool pool{co_await capy::this_coro::executor, create_pool_params(1)};

    co_await with_pool_running(pool, [&]() -> capy::task<> {
        // Check out a dedicated connection and keep it
        auto [ec, conn] = co_await pool.get_connection();
        if (!check_success(ec) || !BOOST_TEST(conn.valid()))
            co_return;
        const auto dedicated_pid = conn->state().backend_process_id;

        // Requests run on a different session, even though we hold the dedicated one
        const auto multiplexed_pid = co_await pool_backend_pid(pool);
        if (!BOOST_TEST_NE(multiplexed_pid, 0))
            co_return;
        BOOST_TEST_NE(multiplexed_pid, dedicated_pid);
    }());
}

//
// Failures. The key point is that a request that goes wrong doesn't compromise
// the requests that share its connection.
//

// A request rejected by the server reports the error, and leaves the connection usable
capy::task<> test_server_error()
{
    // A single connection, so the next request is guaranteed to use the same session
    co_connection_pool pool{co_await capy::this_coro::executor, create_pool_params(1)};

    co_await with_pool_running(pool, [&]() -> capy::task<> {
        // Record the session we're using
        const auto pid = co_await pool_backend_pid(pool);
        if (!BOOST_TEST_NE(pid, 0))
            co_return;

        // Run a request that the server rejects
        request req;
        req.add_query("SELECT * FROM nativepg_nonexistent_table");
        diagnostics diag;
        auto [ec] = co_await pool.exec(req, check(), &diag);
        test_cond_eq(ec, sqlstate_cond::undefined_table);
        BOOST_TEST_NOT(diag.message().empty());

        // The connection is still usable, and wasn't re-established
        if (!co_await check_pool_usable(pool))
            co_return;
        BOOST_TEST_EQ(co_await pool_backend_pid(pool), pid);
    }());
}

// A request rejected by the server doesn't affect a request sharing its connection
capy::task<> test_server_error_with_parallel_request()
{
    // A single connection, so both requests are guaranteed to share a session
    co_connection_pool pool{co_await capy::this_coro::executor, create_pool_params(1)};

    co_await with_pool_running(pool, [&]() -> capy::task<> {
        request req_bad;
        req_bad.add_query("SELECT * FROM nativepg_nonexistent_table");

        request req_good;
        req_good.add_query("SELECT $1 AS value", "abcd");
        std::vector<row_string> strings;

        auto [dummy, res_bad, res_good] = co_await capy::when_all(
            do_pool_exec(pool, req_bad, check()),
            do_pool_exec(pool, req_good, into(strings))
        );

        // Each request got its own outcome
        test_cond_eq(res_bad.code, sqlstate_cond::undefined_table);
        if (!check_success(res_good))
            co_return;
        test_range_eq(strings, std::vector<row_string>{{.value = "abcd"}});

        co_await check_pool_usable(pool);
    }());
}

// A request that doesn't match its handler fails without reaching the server
capy::task<> test_incompatible_handler()
{
    co_connection_pool pool{co_await capy::this_coro::executor, create_pool_params(1)};

    co_await with_pool_running(pool, [&]() -> capy::task<> {
        // The request produces two resultsets, but the handler only consumes one
        request req;
        req.add_query("SELECT $1 AS value", 42);
        req.add_query("SELECT $1 AS value", 10);
        std::vector<row_int> ints;

        auto [ec] = co_await pool.exec(req, into(ints));
        BOOST_TEST_EQ(ec, make_error_code(client_errc::incompatible_response_length));

        // We never wrote anything, so the connection is still good
        co_await check_pool_usable(pool);
    }());
}

//
// Cancellation
//

// A request cancelled while it's being executed fails, and doesn't
// compromise the connection it was using
capy::task<> test_cancel_running_exec()
{
    // A single connection, so the follow-up request uses the same session
    co_connection_pool pool{co_await capy::this_coro::executor, create_pool_params(1)};

    co_await with_pool_running(pool, [&]() -> capy::task<> {
        // Make sure a connection is established, so the cancellation below
        // doesn't happen while waiting for one
        if (!co_await check_pool_usable(pool))
            co_return;

        // Run a request that gets cancelled immediately
        request req;
        req.add_query("SELECT $1 AS value", 42);
        static_cast<void>(co_await capy::when_all(
            [&]() -> capy::task<capy::results<>> {
                auto [ec] = co_await pool.exec(req, check());
                test_cond_eq(ec, capy::cond::canceled);
                co_return {};
            }(),
            capy::ready(std::make_error_code(std::errc::io_error))
        ));

        // Further requests still work
        co_await check_pool_usable(pool);
    }());
}

// A request cancelled while waiting for a healthy connection fails
capy::task<> test_cancel_exec_waiting_for_connection()
{
    // The pool can never establish a connection
    co_connection_pool pool{co_await capy::this_coro::executor, create_unreachable_pool_params()};

    co_await with_pool_running(pool, [&]() -> capy::task<> {
        // No connection will ever become healthy, so this waits until cancelled
        request req;
        req.add_query("SELECT $1 AS value", 42);
        std::stop_source src;
        src.request_stop();
        auto [ec] = co_await capy::run(src.get_token())(pool.exec(req, check()));
        test_cond_eq(ec, capy::cond::canceled);
    }());
}

// A request cancelled with part of its response unread doesn't affect
// further requests sharing its connection
capy::task<> test_cancel_with_leftovers()
{
    // A single connection, so the follow-up request is guaranteed to hit
    // the connection that owes us a response
    co_connection_pool pool{co_await capy::this_coro::executor, create_pool_params(1)};

    // A connection outside the pool, used to hold an advisory lock.
    // The pool's request blocks on it, so the cancellation is guaranteed to
    // arrive with part of the response already read
    auto conn_lock = co_await establish_connection();
    request req_lock;
    req_lock.add_query("SELECT pg_advisory_lock($1)", lock_id_leftovers);
    if (!co_await checked_exec(conn_lock, req_lock))
        co_return;

    co_await with_pool_running(pool, [&]() -> capy::task<> {
        // The first query completes, then the second one blocks on the lock
        request req;
        req.add_query("SELECT $1 AS value", 42);
        req.add_query("SELECT pg_advisory_lock($1)", lock_id_leftovers);

        capy::async_event select_finished;
        capy::async_event exec_finished;
        std::stop_source stop_src;

        // Recall that when_all launches things in order
        static_cast<void>(co_await capy::when_all(
            capy::run(stop_src.get_token())([&]() -> capy::task<capy::results<>> {
                // Blocks on the lock and gets cancelled, leaving leftovers
                row_int row{};
                auto cb = [&](row_int r) {
                    row = r;
                    select_finished.set();
                };
                auto [ec] = co_await pool.exec(
                    req,
                    response{resultset_callback<row_int>(cb), check_execute()}
                );
                test_cond_eq(ec, capy::cond::canceled);
                BOOST_TEST_EQ(row, row_int{.value = 42});

                // Notify downstream tasks
                exec_finished.set();
                co_return {};
            }()),
            [&]() -> capy::task<capy::results<>> {
                // Wait for the first SELECT to finish, then cancel the request
                auto [ec] = co_await select_finished.wait();
                BOOST_TEST_EQ(ec, std::error_code());
                stop_src.request_stop();
                co_return {};
            }(),
            [&]() -> capy::task<capy::results<>> {
                // Once the request is gone, release the lock so its leftovers
                // can be produced by the server, and then skipped
                auto [ec] = co_await exec_finished.wait();
                BOOST_TEST_EQ(ec, std::error_code());
                check_success(co_await conn_lock.shutdown());
                co_return {};
            }()
        ));

        // The leftovers are skipped, and the connection keeps working
        co_await check_pool_usable(pool);
    }());
}

// Requests issued after run() is cancelled fail
capy::task<> test_exec_after_run_cancel()
{
    co_connection_pool pool{co_await capy::this_coro::executor, create_pool_params()};

    // Run the pool but exit immediately
    auto [ec] = co_await corosio::timeout(pool.run(), 1ms);
    test_cond_eq(ec, capy::cond::timeout);

    // Subsequent requests fail
    request req;
    req.add_query("SELECT $1 AS value", 42);
    auto [ec2] = co_await pool.exec(req, check());
    test_cond_eq(ec2, capy::cond::canceled);
}

// Requests waiting for a healthy connection when run() is cancelled fail
capy::task<> test_exec_during_run_cancel()
{
    // The pool can never establish a connection, so the request below
    // is guaranteed to be waiting when the cancellation arrives
    co_connection_pool pool{co_await capy::this_coro::executor, create_unreachable_pool_params()};

    std::stop_source run_stop_src;

    // Recall that when_all launches things in order
    static_cast<void>(co_await capy::when_all(
        [&]() -> capy::task<capy::results<>> {
            auto [ec] = co_await capy::run(run_stop_src.get_token())(pool.run());
            test_cond_eq(ec, capy::cond::canceled);
            co_return {};
        }(),
        [&]() -> capy::task<capy::results<>> {
            // Waits for a healthy connection that never arrives
            request req;
            req.add_query("SELECT $1 AS value", 42);
            auto [ec] = co_await pool.exec(req, check());
            test_cond_eq(ec, capy::cond::canceled);
            co_return {};
        }(),
        [&]() -> capy::task<capy::results<>> {
            // Give the request above a chance to start waiting, then cancel run()
            co_await yield();
            run_stop_src.request_stop();
            co_return {};
        }()
    ));
}

}  // namespace

int main()
{
    run_coroutine_test(test_success());
    run_coroutine_test(test_exec_before_run());
    run_coroutine_test(test_exec_waits_for_healthy_connection());
    run_coroutine_test(test_parallel_execs_single_connection());
    run_coroutine_test(test_parallel_execs_several_connections());
    run_coroutine_test(test_connections_reused());
    run_coroutine_test(test_dedicated_and_multiplexed_disjoint());

    run_coroutine_test(test_server_error());
    run_coroutine_test(test_server_error_with_parallel_request());
    run_coroutine_test(test_incompatible_handler());

    run_coroutine_test(test_cancel_running_exec());
    run_coroutine_test(test_cancel_exec_waiting_for_connection());
    run_coroutine_test(test_cancel_with_leftovers());
    run_coroutine_test(test_exec_after_run_cancel());
    run_coroutine_test(test_exec_during_run_cancel());

    return boost::report_errors();
}
