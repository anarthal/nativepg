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
#include <vector>

#include "nativepg/co_connection.hpp"
#include "nativepg/co_connection_pool.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/request.hpp"
#include "nativepg/responses/check.hpp"
#include "nativepg/responses/into.hpp"
#include "nativepg/responses/response.hpp"
#include "nativepg/responses/response_handler.hpp"
#include "nativepg/responses/resultset_callback.hpp"
#include "nativepg/sqlstate_cond.hpp"
#include "test_utils/ci_server.hpp"
#include "test_utils/co_connection_pool_utils.hpp"
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
    diagnostics diag;
    if (!check_success(co_await pool.exec(req, into(ints), &diag), diag, loc))
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
    diagnostics diag;
    if (!check_success(co_await pool.exec(req, into(ints), &diag), diag, loc))
        co_return 0;
    if (!BOOST_TEST_EQ(ints.size(), 1u))
        co_return 0;
    co_return ints[0].value;
}

//
// Basic API, success cases
//

// exec() works in the sunny-day case
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
        diagnostics diag;

        if (!check_success(co_await pool.exec(req, response{into(ints), into(strings)}, &diag), diag))
            co_return;

        test_range_eq(ints, std::vector<row_int>{{.value = 52}});
        test_range_eq(strings, std::vector<row_string>{{.value = "abcd"}});

        // Subsequent requests succeed, too
        co_await check_pool_usable(pool);
    }());
}

// The version without diagnostics works
capy::task<> test_success_without_diagnostics()
{
    co_connection_pool pool{co_await capy::this_coro::executor, create_pool_params()};

    co_await with_pool_running(pool, [&]() -> capy::task<> {
        request req;
        req.add_query("SELECT 42 AS value", 42);
        std::vector<row_int> ints;

        if (!check_success(co_await pool.exec(req, into(ints))))
            co_return;

        test_range_eq(ints, std::vector<row_int>{{.value = 42}});

        // Subsequent requests succeed, too
        co_await check_pool_usable(pool);
    }());
}

// The type-erased version works
capy::task<> test_success_type_erased()
{
    co_connection_pool pool{co_await capy::this_coro::executor, create_pool_params()};

    co_await with_pool_running(pool, [&]() -> capy::task<> {
        request req;
        req.add_query("SELECT 42 AS value", 42);
        std::vector<row_int> ints;
        auto handler = into(ints);
        diagnostics diag;

        if (!check_success(co_await pool.exec(req, &handler, &diag), diag))
            co_return;

        test_range_eq(ints, std::vector<row_int>{{.value = 42}});

        // Subsequent requests succeed, too
        co_await check_pool_usable(pool);
    }());
}

// The type-erased version without diagnostics works
capy::task<> test_success_type_erased_without_diagnostics()
{
    co_connection_pool pool{co_await capy::this_coro::executor, create_pool_params()};

    co_await with_pool_running(pool, [&]() -> capy::task<> {
        request req;
        req.add_query("SELECT 42 AS value", 42);
        std::vector<row_int> ints;
        auto handler = into(ints);

        if (!check_success(co_await pool.exec(req, &handler)))
            co_return;

        test_range_eq(ints, std::vector<row_int>{{.value = 42}});

        // Subsequent requests succeed, too
        co_await check_pool_usable(pool);
    }());
}

//
// Success cases, advanced scenarios
//

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
            diagnostics diag;
            if (check_success(co_await pool.exec(req, into(ints), &diag), diag))
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

// If no connection is healthy, exec() waits
capy::task<> test_exec_waits_for_healthy_connection()
{
    // The pool can never establish a connection
    co_connection_pool pool{co_await capy::this_coro::executor, create_unreachable_pool_params()};

    co_await with_pool_running(pool, [&]() -> capy::task<> {
        // No connection will ever become healthy, so the request waits until it times out
        request req;
        req.add_query("SELECT $1 AS value", 42);
        auto [ec] = co_await corosio::timeout(pool.exec(req, check()), 100ms);
        test_cond_eq(ec, capy::cond::timeout);
    }());
}

// Several requests can be run in parallel
capy::task<> test_parallel_execs_single_connection()
{
    co_connection_pool pool{co_await capy::this_coro::executor, create_pool_params(2)};

    co_await with_pool_running(pool, [&]() -> capy::task<> {
        // All three requests are handed to the pool before any completes
        static_cast<void>(co_await capy::when_all(
            [&]() -> capy::task<capy::results<>> {
                request req;
                req.add_query("SELECT $1 + $2 AS value", 42, 10);
                std::vector<row_int> ints;

                if (!check_success(co_await pool.exec(req, into(ints))))
                    co_return {};

                test_range_eq(ints, std::vector<row_int>{{.value = 52}});
                co_return {};
            }(),
            [&]() -> capy::task<capy::results<>> {
                request req;
                req.add_query("SELECT $1 AS value", 7);
                req.add_query("SELECT $1 AS value", "hello");
                std::vector<row_int> ints;
                std::vector<row_string> strings;

                if (!check_success(co_await pool.exec(req, response{into(ints), into(strings)})))
                    co_return {};

                test_range_eq(ints, std::vector<row_int>{{.value = 7}});
                test_range_eq(strings, std::vector<row_string>{{.value = "hello"}});
                co_return {};
            }(),
            [&]() -> capy::task<capy::results<>> {
                request req;
                req.add_query("SELECT $1 AS value", "world!");
                std::vector<row_string> strings;

                if (!check_success(co_await pool.exec(req, into(strings))))
                    co_return {};

                test_range_eq(strings, std::vector<row_string>{{.value = "world!"}});
                co_return {};
            }()
        ));

        co_await check_pool_usable(pool);
    }());
}

// Same, with several multiplexed connections available
capy::task<> test_parallel_execs_several_connections()
{
    co_connection_pool pool{co_await capy::this_coro::executor, create_pool_params(2)};

    co_await with_pool_running(pool, [&]() -> capy::task<> {
        static_cast<void>(co_await capy::when_all(
            [&]() -> capy::task<capy::results<>> {
                request req;
                req.add_query("SELECT $1 + $2 AS value", 42, 10);
                std::vector<row_int> ints;

                if (!check_success(co_await pool.exec(req, into(ints))))
                    co_return {};

                test_range_eq(ints, std::vector<row_int>{{.value = 52}});
                co_return {};
            }(),
            [&]() -> capy::task<capy::results<>> {
                request req;
                req.add_query("SELECT $1 AS value", 7);
                req.add_query("SELECT $1 AS value", "hello");
                std::vector<row_int> ints;
                std::vector<row_string> strings;

                if (!check_success(co_await pool.exec(req, response{into(ints), into(strings)})))
                    co_return {};

                test_range_eq(ints, std::vector<row_int>{{.value = 7}});
                test_range_eq(strings, std::vector<row_string>{{.value = "hello"}});
                co_return {};
            }(),
            [&]() -> capy::task<capy::results<>> {
                request req3;
                req3.add_query("SELECT $1 AS value", "world!");
                std::vector<row_string> strings3;

                if (!check_success(co_await pool.exec(req3, into(strings3))))
                    co_return {};

                test_range_eq(strings3, std::vector<row_string>{{.value = "world!"}});
                co_return {};
            }()
        ));

        co_await check_pool_usable(pool);
    }());
}

// The pool doesn't create a connection per request
capy::task<> test_connections_reused()
{
    constexpr std::size_t num_connections = 2;
    co_connection_pool pool{co_await capy::this_coro::executor, create_pool_params(num_connections)};

    co_await with_pool_running(pool, [&]() -> capy::task<> {
        std::set<std::int32_t> pids;

        // Run 6 requests in parallel, where each request inspects
        // the connection's pid
        auto get_pid_task = [&]() -> capy::task<capy::results<>> {
            pids.insert(co_await pool_backend_pid(pool));
            co_return {};
        };
        static_cast<void>(co_await capy::when_all(
            get_pid_task(),
            get_pid_task(),
            get_pid_task(),
            get_pid_task(),
            get_pid_task(),
            get_pid_task()
        ));

        // We didn't create one connection per request
        BOOST_TEST_LE(pids.size(), num_connections);
    }());
}

//
// Errors and cancellations. An error in a request shouldn't impact subsequent ones.
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
        static_cast<void>(co_await capy::when_all(
            [&]() -> capy::task<capy::results<>> {
                // The bad request goes first
                request req;
                req.add_query("SELECT * FROM nativepg_nonexistent_table");

                auto [ec] = co_await pool.exec(req, check());

                test_cond_eq(ec, sqlstate_cond::undefined_table);
                co_return {};
            }(),
            [&]() -> capy::task<capy::results<>> {
                // The good request goes after it
                request req;
                req.add_query("SELECT $1 AS value", "abcd");
                std::vector<row_string> strings;

                if (!check_success(co_await pool.exec(req, into(strings))))
                    co_return {};

                test_range_eq(strings, std::vector<row_string>{{.value = "abcd"}});
                co_return {};
            }()
        ));

        co_await check_pool_usable(pool);
    }());
}

// A request cancelled while waiting for a healthy connection fails
capy::task<> test_cancel_waiting_for_connection()
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

// A request cancelled while running exec() fails.
// Leftovers don't cause trouble to successive requests.
capy::task<> test_cancel_executing()
{
    // A single connection, so both requests use the same connection
    co_connection_pool pool{co_await capy::this_coro::executor, create_pool_params(1)};

    // Acquire a lock to guarantee that we cancel with part of the response unread
    auto conn_lock = co_await establish_connection();
    request req_lock;
    constexpr int lock_id = 1001;  // should be unique
    req_lock.add_query("SELECT pg_advisory_lock($1)", lock_id);
    if (!co_await checked_exec(conn_lock, req_lock))
        co_return;

    co_await with_pool_running(pool, [&]() -> capy::task<> {
        capy::async_event select_finished;
        capy::async_event exec_finished;
        std::stop_source stop_src;
        std::int32_t pid1 = 0, pid2 = 0;

        // Recall that when_all launches things in order
        static_cast<void>(co_await capy::when_all(
            capy::run(stop_src.get_token())([&]() -> capy::task<capy::results<>> {
                // Blocks on the lock and gets cancelled, leaving leftovers
                request req;
                req.add_query("SELECT pg_backend_pid() AS value");
                req.add_query("SELECT pg_advisory_lock($1)", lock_id);
                auto cb = [&](row_int r) {
                    pid1 = r.value;
                    select_finished.set();
                };
                auto [ec] = co_await pool.exec(
                    req,
                    response{resultset_callback<row_int>(cb), check_execute()}
                );
                test_cond_eq(ec, capy::cond::canceled);

                // Notify downstream tasks
                exec_finished.set();
                co_return {};
            }()),
            [&]() -> capy::task<capy::results<>> {
                // Run a 2nd request, in parallel, that checks the connection PID.
                // Will be multiplexed after the 1st, blocking one
                pid2 = co_await pool_backend_pid(pool);
                co_return {};
            }(),
            [&]() -> capy::task<capy::results<>> {
                // Wait for the first SELECT to finish, then cancel the request
                auto [ec] = co_await select_finished.wait();
                BOOST_TEST_EQ(ec, std::error_code());
                stop_src.request_stop();
                co_return {};
            }(),
            [&]() -> capy::task<capy::results<>> {
                // Release the lock so other requests can make progress
                check_success(co_await exec_finished.wait());
                check_success(co_await conn_lock.shutdown());
                co_return {};
            }()
        ));

        // The leftovers were skipped and allowed the request getting the PID to succeed
        BOOST_TEST_NE(pid1, 0);
        BOOST_TEST_EQ(pid1, pid2);

        // The pool is OK
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
            // Run the pool for a very small period of time
            auto [ec] = co_await corosio::timeout(pool.run(), 1ms);
            test_cond_eq(ec, capy::cond::timeout);
            co_return {};
        }(),
        [&]() -> capy::task<capy::results<>> {
            // Waits for a healthy connection that never arrives
            request req;
            req.add_query("SELECT $1 AS value", 42);
            auto [ec] = co_await pool.exec(req, check());
            test_cond_eq(ec, capy::cond::canceled);
            co_return {};
        }()
    ));
}

}  // namespace

int main()
{
    run_coroutine_test(test_success());
    run_coroutine_test(test_success_without_diagnostics());
    run_coroutine_test(test_success_type_erased());
    run_coroutine_test(test_success_type_erased_without_diagnostics());

    run_coroutine_test(test_exec_before_run());
    run_coroutine_test(test_exec_waits_for_healthy_connection());
    run_coroutine_test(test_parallel_execs_single_connection());
    run_coroutine_test(test_parallel_execs_several_connections());
    run_coroutine_test(test_connections_reused());

    run_coroutine_test(test_server_error());
    run_coroutine_test(test_server_error_with_parallel_request());
    run_coroutine_test(test_cancel_waiting_for_connection());
    run_coroutine_test(test_cancel_executing());
    run_coroutine_test(test_exec_after_run_cancel());
    run_coroutine_test(test_exec_during_run_cancel());

    return boost::report_errors();
}
