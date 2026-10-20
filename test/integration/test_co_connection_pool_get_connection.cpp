//
// Copyright (c) 2019-2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/assert/source_location.hpp>
#include <boost/capy/cond.hpp>
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
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "nativepg/co_connection.hpp"
#include "nativepg/co_connection_pool.hpp"
#include "nativepg/protocol/connection_state.hpp"
#include "nativepg/request.hpp"
#include "nativepg/responses/check.hpp"
#include "nativepg/responses/into.hpp"
#include "test_utils/ci_server.hpp"
#include "test_utils/co_connection_utils.hpp"
#include "test_utils/corosio_utils.hpp"
#include "test_utils/printing.hpp"
#include "test_utils/test_cond_eq.hpp"
#include "test_utils/test_string_contains.hpp"
#include "test_utils/yield.hpp"

namespace capy = boost::capy;
namespace corosio = boost::corosio;
using namespace nativepg;
using namespace nativepg::test;
using namespace std::chrono_literals;
using namespace std::string_view_literals;

namespace {

struct row_string
{
    std::string value;
};
BOOST_DESCRIBE_STRUCT(row_string, (), (value))

using boost::describe::operators::operator==;
using boost::describe::operators::operator<<;

pool_params create_pool_params(std::size_t max_size = 151)
{
    return {
        .transport = default_connect_params(),
        .max_size = max_size,
    };
}

// --- Running a pool in the background ---

// The pool has no cancel() function: run() keeps going until its stop token
// fires. Requesting the stop from a destructor means that a body that bails
// out early (or throws) still lets run() - and thus the test - finish.
struct stop_on_exit
{
    std::stop_source* src;
    ~stop_on_exit() { src->request_stop(); }
};

enum class start_order
{
    // The usual case: the pool is already running when the body starts
    run_first,

    // The body gets to issue its requests before run() starts
    body_first,
};

// Runs body(pool) with pool.run() active in the background, then cancels the
// pool and waits for run() to finish. body must return capy::task<>.
template <class BodyFn>
capy::task<> with_pool(pool_params params, BodyFn body, start_order order = start_order::run_first)
{
    co_connection_pool pool{co_await capy::this_coro::executor, std::move(params)};
    std::stop_source cancel_pool;

    auto run_pool = [&]() -> capy::task<capy::results<>> {
        // run() reports the cancellation that terminated it
        auto [ec] = co_await capy::run(cancel_pool.get_token())(pool.run());
        test_cond_eq(ec, capy::cond::canceled);
        co_return {};
    };

    auto run_body = [&]() -> capy::task<capy::results<>> {
        stop_on_exit guard{&cancel_pool};
        co_await body(pool);
        co_return {};
    };

    // Recall that when_all launches its children in order
    if (order == start_order::run_first)
        static_cast<void>(co_await capy::when_all(run_pool(), run_body()));
    else
        static_cast<void>(co_await capy::when_all(run_body(), run_pool()));
}

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
            co_await std::move(body);
            co_return {};
        }()
    ));
}

// --- Telling connections apart and inspecting session state ---

// Every backend serving us has a distinct process id, which the connection
// learns during startup. It's the cheapest way to check whether two
// pooled_connection's are backed by the same session.
std::int32_t backend_pid(pooled_connection& conn) { return conn.get().state().backend_process_id; }

// Postgres accepts arbitrary settings under a prefix, so we can leave a mark
// on a session and look for it after the connection has gone back to the pool
capy::task<bool> set_session_var(
    co_connection& conn,
    std::string_view value,
    boost::source_location loc = BOOST_CURRENT_LOCATION
)
{
    request req;
    req.add_simple_query(std::string("SET nativepg.pool_get_connection_var = '") + std::string(value) + "'");
    co_return co_await checked_exec(conn, req, check(), loc);
}

// Returns "<unset>" if the variable was never set in this session
capy::task<std::string> get_session_var(
    co_connection& conn,
    boost::source_location loc = BOOST_CURRENT_LOCATION
)
{
    request req;
    req.add_query(
        "SELECT coalesce(current_setting('nativepg.pool_get_connection_var', true), '<unset>') AS value"
    );
    std::vector<row_string> rows;
    if (!co_await checked_exec(conn, req, into(rows), loc))
        co_return "<exec failed>";
    if (!BOOST_TEST_EQ(rows.size(), 1u))
        co_return "<unexpected resultset>";
    co_return rows[0].value;
}

// --- Tests ---

// A connection can be returned to the pool and is re-used without re-establishing.
capy::task<> test_return_connection()
{
    // max_size is 1, so the pool can only ever hand us the same connection
    co_connection_pool pool{co_await capy::this_coro::executor, create_pool_params(1)};

    co_await with_pool_running(pool, [&]() -> capy::task<> {
        // Get a connection
        auto [ec, conn] = co_await pool.get_connection();
        if (!check_success(ec) || !BOOST_TEST(conn.valid()))
            co_return;
        const auto pid = conn->state().backend_process_id;

        // Alter session state
        if (!co_await set_session_var(conn.get(), "abc"))
            co_return;

        // Return the connection
        conn.return_without_reset();
        BOOST_TEST_NOT(conn.valid());

        // Get the same connection again
        auto [ec2, conn2] = co_await pool.get_connection();
        if (!check_success(ec2) || !BOOST_TEST(conn2.valid()))
            co_return;
        BOOST_TEST_EQ(conn2->state().backend_process_id, pid);

        // No reset has been issued, so our mark is still there
        BOOST_TEST_EQ(co_await get_session_var(conn2.get()), "abc"sv);
    }());
}

// pooled_connection's destructor also returns the connection to the pool
capy::task<> test_pooled_connection_destructor()
{
    // max_size is 1, so the pool can only ever hand us the same connection
    co_connection_pool pool{co_await capy::this_coro::executor, create_pool_params(1)};

    co_await with_pool_running(pool, [&]() -> capy::task<> {
        std::int32_t pid = 0;

        {
            // Get a connection
            auto [ec, conn] = co_await pool.get_connection();
            if (!check_success(ec) || !BOOST_TEST(conn.valid()))
                co_return;
            pid = conn->state().backend_process_id;

            // Alter session state
            if (!co_await set_session_var(conn.get(), "abc"))
                co_return;

            // The destructor returns the connection
        }

        // Get the same connection again
        auto [ec, conn] = co_await pool.get_connection();
        if (!check_success(ec) || !BOOST_TEST(conn.valid()))
            co_return;
        BOOST_TEST_EQ(conn->state().backend_process_id, pid);

        // No reset has been issued, so our mark is still there
        BOOST_TEST_EQ(co_await get_session_var(conn.get()), "abc"sv);
    }());
}

// Asking for a connection while another one is in use creates a new one
capy::task<> test_connections_created_if_required()
{
    co_connection_pool pool{co_await capy::this_coro::executor, create_pool_params()};

    co_await with_pool_running(pool, [&]() -> capy::task<> {
        // Get a connection
        auto [ec1, conn1] = co_await pool.get_connection();
        if (!check_success(ec1) || !BOOST_TEST(conn1.valid()))
            co_return;
        if (!co_await check_connection_usable(conn1.get()))
            co_return;

        // Get another one. Since the first is still in use, this creates a new connection
        auto [ec2, conn2] = co_await pool.get_connection();
        if (!check_success(ec2) || !BOOST_TEST(conn2.valid()))
            co_return;
        if (!co_await check_connection_usable(conn2.get()))
            co_return;

        // They are backed by different sessions
        BOOST_TEST_NE(conn1->state().backend_process_id, conn2->state().backend_process_id);
    }());
}

// If there are more outstanding requests than pending connections,
// connections are created
capy::task<> test_connections_created_if_requests_gt_pending()
{
    co_connection_pool pool{co_await capy::this_coro::executor, create_pool_params()};

    co_await with_pool_running(pool, [&]() -> capy::task<> {
        // Request several connections in parallel
        auto [ec, conn1, conn2, conn3] = co_await capy::when_all(
            pool.get_connection(),
            pool.get_connection(),
            pool.get_connection()
        );
        if (!check_success(ec))
            co_return;
        if (!BOOST_TEST(conn1.valid()) || !BOOST_TEST(conn2.valid()) || !BOOST_TEST(conn3.valid()))
            co_return;

        // They should be different connections
        BOOST_TEST_NE(backend_pid(conn1), backend_pid(conn2));
        BOOST_TEST_NE(backend_pid(conn1), backend_pid(conn3));
        BOOST_TEST_NE(backend_pid(conn2), backend_pid(conn3));
    }());
}

// The pool never creates more than max_size connections
capy::task<> test_connection_upper_limit()
{
    co_connection_pool pool{co_await capy::this_coro::executor, create_pool_params(1)};

    co_await with_pool_running(pool, [&]() -> capy::task<> {
        // Get the only connection the pool is allowed to create
        auto [ec, conn] = co_await pool.get_connection();
        if (!check_success(ec) || !BOOST_TEST(conn.valid()))
            co_return;

        // Getting another one blocks until we return the one we have.
        // Since we don't, the request times out
        auto [ec2, conn2] = co_await corosio::timeout(pool.get_connection(), 50ms);
        test_cond_eq(ec2, capy::cond::timeout);
        BOOST_TEST_NOT(conn2.valid());
    }());
}

// Connections requested before run() starts wait, rather than failing
capy::task<> test_get_connection_before_run()
{
    co_connection_pool pool{co_await capy::this_coro::executor, create_pool_params()};

    static_cast<void>(co_await capy::when_any(
        [&]() -> capy::task<capy::results<>> {
            // Ask for two connections. The pool isn't running yet, so both wait
            auto [ec, conn1, conn2] = co_await capy::when_all(pool.get_connection(), pool.get_connection());
            if (!check_success(ec))
                co_return {};
            if (!BOOST_TEST(conn1.valid()) || !BOOST_TEST(conn2.valid()))
                co_return {};

            // They're different connections
            BOOST_TEST_NE(backend_pid(conn1), backend_pid(conn2));
            co_return {};
        }(),
        [&]() -> capy::task<capy::results<>> {
            co_await yield();  // give some margin to the tasks
            auto [ec] = co_await pool.run();
            test_cond_eq(ec, capy::cond::canceled);
            co_return {};
        }()
    ));
}

// // Cancelling makes run() return, even with a connection still checked out
// capy::task<> test_cancel_run()
// {
//     co_connection_pool pool{co_await capy::this_coro::executor, create_pool_params()};
//     std::stop_source cancel_pool;

//     // Declared after the pool so it's destroyed first: returning a connection
//     // touches the node, which the pool owns
//     pooled_connection held{};

//     static_cast<void>(co_await capy::when_all(
//         [&]() -> capy::task<capy::results<>> {
//             auto [ec] = co_await capy::run(cancel_pool.get_token())(pool.run());
//             test_cond_eq(ec, capy::cond::canceled);
//             co_return {};
//         }(),

//         [&]() -> capy::task<capy::results<>> {
//             // Hold on to a connection while the pool is cancelled
//             auto [ec, conn] = co_await pool.get_connection();
//             if (check_success(ec) && BOOST_TEST(conn.valid()))
//                 held = std::move(conn);
//             cancel_pool.request_stop();
//             co_return {};
//         }()
//     ));

//     // Cancelling again does nothing
//     cancel_pool.request_stop();
// }

// // If the pool is cancelled before run() is awaited, the cancellation still has effect
// capy::task<> test_cancel_before_run()
// {
//     co_connection_pool pool{co_await capy::this_coro::executor, create_pool_params()};
//     std::stop_source cancel_pool;

//     // Cancel before running
//     cancel_pool.request_stop();

//     // run() tears down whatever it created and returns
//     auto [ec] = co_await capy::run(cancel_pool.get_token())(pool.run());
//     test_cond_eq(ec, capy::cond::canceled);

//     // The pool is cancelled, so requests fail
//     auto [ec2, conn] = co_await pool.get_connection();
//     test_cond_eq(ec2, capy::cond::canceled);
//     BOOST_TEST_NOT(conn.valid());
// }

// // Cancelling the pool makes outstanding get_connection requests fail,
// // and further ones fail immediately
// capy::task<> test_cancel_get_connection()
// {
//     // max_size is 1, so the second request below has nothing to be served with
//     co_connection_pool pool{co_await capy::this_coro::executor, create_pool_params(1)};
//     std::stop_source cancel_pool;

//     // Declared after the pool so it's destroyed first
//     pooled_connection held{};

//     static_cast<void>(co_await capy::when_all(
//         [&]() -> capy::task<capy::results<>> {
//             auto [ec] = co_await capy::run(cancel_pool.get_token())(pool.run());
//             test_cond_eq(ec, capy::cond::canceled);
//             co_return {};
//         }(),

//         // Takes the only connection, then cancels the pool while holding it
//         [&]() -> capy::task<capy::results<>> {
//             auto [ec, conn] = co_await pool.get_connection();
//             if (check_success(ec) && BOOST_TEST(conn.valid()))
//                 held = std::move(conn);
//             cancel_pool.request_stop();
//             co_return {};
//         }(),

//         // Launched after the request above, so it's still waiting when the
//         // cancellation arrives
//         [&]() -> capy::task<capy::results<>> {
//             auto [ec, conn] = co_await pool.get_connection();
//             test_cond_eq(ec, capy::cond::canceled);
//             BOOST_TEST_NOT(conn.valid());
//             co_return {};
//         }()
//     ));

//     // Calling get_connection after the cancellation fails too
//     auto [ec, conn] = co_await pool.get_connection();
//     test_cond_eq(ec, capy::cond::canceled);
//     BOOST_TEST_NOT(conn.valid());
// }

// // Spotcheck: the pool works with zero timeouts, which disable the
// // relevant timer rather than expiring immediately
// capy::task<> test_zero_timeouts()
// {
//     auto params = create_pool_params(1);  // max_size 1, so we force a reset
//     params.connect_timeout = 0s;
//     params.ping_timeout = 0s;
//     params.ping_interval = 0s;

//     co_await with_pool(std::move(params), [](co_connection_pool& pool) -> capy::task<> {
//         // Get a connection and use it
//         {
//             auto [ec, conn] = co_await pool.get_connection();
//             if (!check_success(ec))
//                 co_return;
//             if (!BOOST_TEST(conn.valid()))
//                 co_return;
//             co_await check_connection_usable(conn.get());
//         }

//         // Returning it goes through a reset, and the connection can be used again
//         auto [ec2, conn2] = co_await pool.get_connection();
//         if (!check_success(ec2))
//             co_return;
//         if (!BOOST_TEST(conn2.valid()))
//             co_return;
//         co_await check_connection_usable(conn2.get());
//     });
// }

// // Spotcheck: constructing a pool with invalid params throws
// capy::task<> test_invalid_params()
// {
//     auto ex = co_await capy::this_coro::executor;

//     auto check_throws = [ex](pool_params params, std::string_view expected_msg) {
//         try
//         {
//             static_cast<void>(co_connection_pool(ex, std::move(params)));
//             BOOST_ERROR("Expected std::invalid_argument, but no exception was thrown");
//         }
//         catch (const std::invalid_argument& exc)
//         {
//             test_string_contains(exc.what(), expected_msg);
//         }
//     };

//     check_throws({.transport = {}, .max_size = 0}, "pool_params::max_size must be greater than zero");
//     check_throws(
//         {.transport = {}, .initial_size = 4, .max_size = 2},
//         "pool_params::max_size must be greater than pool_params::initial_size"
//     );
//     check_throws(
//         {.transport = {}, .connect_timeout = -1s},
//         "pool_params::connect_timeout must not be negative"
//     );
//     check_throws(
//         {.transport = {}, .retry_interval = 0s},
//         "pool_params::retry_interval must be greater than zero"
//     );
//     check_throws(
//         {.transport = {}, .retry_interval = -1s},
//         "pool_params::retry_interval must be greater than zero"
//     );
//     check_throws({.transport = {}, .ping_interval = -1s}, "pool_params::ping_interval must not be
//     negative"); check_throws({.transport = {}, .ping_timeout = -1s}, "pool_params::ping_timeout must not be
//     negative");
// }

}  // namespace

int main()
{
    run_coroutine_test(test_return_connection());
    run_coroutine_test(test_pooled_connection_destructor());
    run_coroutine_test(test_connections_created_if_required());
    run_coroutine_test(test_connections_created_if_requests_gt_pending());
    run_coroutine_test(test_connection_upper_limit());
    run_coroutine_test(test_get_connection_before_run());

    // run_coroutine_test(test_cancel_run());
    // run_coroutine_test(test_cancel_before_run());
    // run_coroutine_test(test_cancel_get_connection());
    // run_coroutine_test(test_zero_timeouts());
    // run_coroutine_test(test_invalid_params());

    return boost::report_errors();
}
