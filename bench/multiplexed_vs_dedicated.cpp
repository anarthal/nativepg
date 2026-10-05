//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

/**
 * Are multiplexed connections faster than dedicated connections?
 * See bench/README.md for a rationale.
 *
 * Both cases use a single co_connection, shared by all sessions. The only
 * difference is how the sessions access it:
 *   - Multiplexed: sessions call exec() concurrently, letting the connection
 *     pipeline the requests.
 *   - Dedicated: sessions take a mutex around exec(), so only one request is
 *     in flight at any given time.
 *
 * Reported figures:
 *   - Latency: wall time from issuing a query to having its response, as a
 *     session sees it. For the dedicated case that includes the time queued
 *     on the mutex, which is the bulk of it under contention.
 *   - Throughput: total wall time of the run divided by the total number of
 *     queries, plus its reciprocal in queries/second. Connection
 *     establishment happens before the clock starts and is excluded.
 *
 * This benchmark expects a Postgres database with an `employee` table:
 *
 *     CREATE TABLE employee (
 *         id          BIGINT PRIMARY KEY,
 *         first_name  TEXT NOT NULL,
 *         last_name   TEXT NOT NULL
 *     );
 */

#include <boost/capy/ex/async_mutex.hpp>
#include <boost/capy/ex/executor_ref.hpp>
#include <boost/capy/ex/run_async.hpp>
#include <boost/capy/ex/this_coro.hpp>
#include <boost/capy/io_task.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/when_all.hpp>
#include <boost/corosio/io_context.hpp>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

#include "bench_utils.hpp"
#include "nativepg/co_connection.hpp"
#include "nativepg/request.hpp"
#include "nativepg/responses/check.hpp"

using namespace nativepg;
using namespace nativepg::bench;
namespace capy = boost::capy;
namespace corosio = boost::corosio;

using clock_type = std::chrono::steady_clock;

namespace {

// Benchmark parameters
constexpr std::string_view query = "SELECT first_name FROM employee WHERE id = $1";
constexpr int nqueries = 1000;
constexpr int nsess = 100;

struct bench_state
{
    co_connection conn;
    capy::async_mutex mtx;  // only used by the dedicated case
    stats latency;          // shared by all sessions

    explicit bench_state(capy::executor_ref ex) : conn(ex) {}
};

// Runs nqueries queries serially, taking the mutex around each one, and folds
// each exec() latency into the shared accumulator.
capy::io_task<> dedicated_session(bench_state& st, std::int64_t session_id)
{
    // Serializing the request once and reusing it keeps request composition
    // out of the measurement.
    request req;
    req.add_query(query, session_id);

    for (int i = 0; i < nqueries; ++i)
    {
        const auto t1 = clock_type::now();

        {
            // Only one session may use the connection at a time
            auto [lock_ec, lock] = co_await st.mtx.scoped_lock();
            if (lock_ec)
                co_return {lock_ec};  // canceled while queued

            if (auto [ec] = co_await st.conn.exec(req, check_execute()); ec)
                die("execute", ec);
        }

        const auto t2 = clock_type::now();

        st.latency.add(std::chrono::duration<double, std::micro>(t2 - t1).count());
    }

    co_return {};
}

// Same as above, but without the mutex: concurrent exec() calls on a single
// connection are well-defined, and get pipelined.
capy::io_task<> multiplexed_session(bench_state& st, std::int64_t session_id)
{
    request req;
    req.add_query(query, session_id);

    for (int i = 0; i < nqueries; ++i)
    {
        const auto t1 = clock_type::now();

        if (auto [ec] = co_await st.conn.exec(req, check_execute()); ec)
            die("execute", ec);

        const auto t2 = clock_type::now();

        st.latency.add(std::chrono::duration<double, std::micro>(t2 - t1).count());
    }

    co_return {};
}

using session_fn = capy::io_task<> (*)(bench_state&, std::int64_t);

capy::task<> run_case(const connect_params& params, const char* name, session_fn make_session)
{
    // Setup
    bench_state st{co_await capy::this_coro::executor};

    // Establishing the connection is not part of the measurement
    if (auto [ec] = co_await st.conn.connect(params); ec)
        die("connect", ec);

    // Discard one query, so that first-query costs don't land in the measurement
    request warmup;
    warmup.add_query(query, static_cast<std::int64_t>(-1));
    if (auto [ec] = co_await st.conn.exec(warmup, check_execute()); ec)
        die("warmup", ec);

    // Tasks are lazy: none of these run until when_all awaits them
    std::vector<capy::io_task<>> sessions;
    sessions.reserve(nsess);
    for (int i = 0; i < nsess; ++i)
        sessions.push_back(make_session(st, i));

    const auto t0 = clock_type::now();
    if (auto [ec] = co_await capy::when_all(std::move(sessions)); ec)
        die("session", ec);
    const auto t1 = clock_type::now();

    print_results(name, 1u, nsess, nqueries, std::chrono::duration<double>(t1 - t0).count(), st.latency);

    if (auto [ec] = co_await st.conn.shutdown(); ec)
        die("shutdown", ec);
}

// params must outlive this coroutine
capy::task<> co_main(const connect_params& params)
{
    co_await run_case(params, "Dedicated connection", &dedicated_session);
    co_await run_case(params, "Multiplexed connection", &multiplexed_session);
}

}  // namespace

int main(int argc, char** argv)
{
    // All arguments are positional and required
    if (argc != 5)
    {
        std::cerr << "Usage: " << argv[0] << " <hostname> <username> <password> <database>\n";
        return 1;
    }
    const connect_params params{
        .hostname = argv[1],
        .username = argv[2],
        .password = argv[3],
        .database = argv[4],
    };

    // The I/O context, required for all I/O operations
    corosio::io_context ctx;

    // Schedules the main coroutine for execution
    capy::run_async(
        ctx.get_executor(),
        []() {
           // Runs when the main coroutine finishes normally
           std::cout << "Done\n";
        },
        [](std::exception_ptr exc) {
            // Runs when the main coroutine finishes with an exception
            try {
               std::rethrow_exception(exc);
            } catch (const std::exception& e) {
               std::cerr << "Error: " << e.what() << std::endl;
            }
            exit(1);
        }
    )(co_main(params));

    // Executes all pending work, including the main coroutine
    ctx.run();
}
