//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

/**
 * Measures latency and throughput of simple SELECT queries run through a
 * single dedicated connection.
 *
 * The benchmark spawns nsess sessions that run concurrently. Each session runs
 * nqueries SELECT queries, one after another. All sessions share a single
 * co_connection; because a dedicated connection can only serve one request at
 * a time, access to it is serialized with a capy::async_mutex.
 *
 * Reported figures:
 *   - Latency: wall time of each individual exec() call. Time spent waiting
 *     for the mutex is *not* included, so these numbers describe the
 *     connection's per-request service time, not the queueing delay a session
 *     observes.
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

#include <boost/assert/source_location.hpp>
#include <boost/capy/ex/async_mutex.hpp>
#include <boost/capy/ex/run_async.hpp>
#include <boost/capy/ex/this_coro.hpp>
#include <boost/capy/io_task.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/when_all.hpp>
#include <boost/corosio/io_context.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "nativepg/co_connection.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/request.hpp"
#include "nativepg/responses/check.hpp"

using namespace nativepg;
namespace capy = boost::capy;
namespace corosio = boost::corosio;

using clock_type = std::chrono::steady_clock;

// Benchmark parameters
constexpr std::string_view query = "SELECT first_name FROM employee WHERE id = $1";
constexpr std::int64_t query_param = 1;
constexpr int nqueries = 1000;
constexpr int nsess = 10;

static connect_params make_connect_params()
{
    return {
        .hostname = "localhost",
        .username = "postgres",
        .password = "secret",
        .database = "postgres",
    };
}

// Turns an unexpected error into an exception. The benchmark has nothing
// meaningful to report if any query fails, so bail out. run_async's exception
// handler prints the message and exits.
static void die_on_error(
    const char* prefix,
    const extended_error& err,
    boost::source_location loc = BOOST_CURRENT_LOCATION
)
{
    if (!err.code)
        return;

    std::string msg{prefix};
    msg += " (";
    msg += loc.file_name();
    msg += ':';
    msg += std::to_string(loc.line());
    msg += ')';
    if (!err.diag.message().empty())
    {
        msg += ": ";
        msg += err.diag.message();
    }
    throw std::system_error(err.code, msg);
}

// Latency summary, in microseconds. Built from the raw per-query samples.
struct stats
{
    std::size_t count{};
    double mean{};
    double min{};
    double max{};
    double p50{};
    double p90{};
    double p99{};
};

// Sorts samples in place and summarizes them
static stats summarize(std::vector<double>& samples)
{
    if (samples.empty())
        return {};

    std::sort(samples.begin(), samples.end());

    const auto percentile = [&samples](double q) {
        const auto n = samples.size();
        auto i = static_cast<std::size_t>(q * static_cast<double>(n));
        return samples[std::min(i, n - 1u)];
    };

    double sum = 0.0;
    for (double s : samples)
        sum += s;

    return {
        .count = samples.size(),
        .mean = sum / static_cast<double>(samples.size()),
        .min = samples.front(),
        .max = samples.back(),
        .p50 = percentile(0.50),
        .p90 = percentile(0.90),
        .p99 = percentile(0.99),
    };
}

// Runs nqueries queries serially, taking the mutex around each one.
// Returns the per-query exec() latencies, in microseconds.
static capy::io_task<std::vector<double>> dedicated_session(co_connection& conn, capy::async_mutex& mtx)
{
    // Serializing the request once and reusing it keeps request composition
    // out of the measurement.
    request req;
    req.add_query(query, query_param);

    diagnostics diag;
    std::vector<double> latencies;
    latencies.reserve(nqueries);

    for (int i = 0; i < nqueries; ++i)
    {
        // Only one session may use the connection at a time
        auto [lock_ec, lock] = co_await mtx.scoped_lock();
        if (lock_ec)
            co_return {lock_ec, {}};  // canceled while queued

        const auto t1 = clock_type::now();
        auto [ec] = co_await conn.exec(req, check_execute(), &diag);
        const auto t2 = clock_type::now();
        die_on_error("execute", {ec, diag});

        latencies.push_back(std::chrono::duration<double, std::micro>(t2 - t1).count());
    }

    co_return {{}, std::move(latencies)};
}

static void print_results(double elapsed_secs, std::vector<double>& latencies)
{
    const auto st = summarize(latencies);
    const auto total_queries = static_cast<double>(st.count);

    std::cout << std::fixed << std::setprecision(2)                                        //
              << "Sessions:            " << nsess << '\n'                                  //
              << "Queries per session: " << nqueries << '\n'                               //
              << "Queries run:         " << st.count << '\n'                               //
              << "\nThroughput\n"                                                          //
              << "  total time:        " << elapsed_secs * 1e3 << " ms\n"                  //
              << "  time per query:    " << elapsed_secs * 1e6 / total_queries << " us\n"  //
              << "  queries/second:    " << total_queries / elapsed_secs << '\n'           //
              << "\nexec() latency (us)\n"                                                 //
              << "  mean:              " << st.mean << '\n'                                //
              << "  min:               " << st.min << '\n'                                 //
              << "  p50:               " << st.p50 << '\n'                                 //
              << "  p90:               " << st.p90 << '\n'                                 //
              << "  p99:               " << st.p99 << '\n'                                 //
              << "  max:               " << st.max << '\n';
}

// Runs all the sessions against a single connection and reports the results.
// The connection must already be established.
static capy::task<> run_dedicated(co_connection& conn)
{
    capy::async_mutex mtx;

    // Tasks are lazy: none of these run until when_all awaits them
    std::vector<capy::io_task<std::vector<double>>> sessions;
    sessions.reserve(nsess);
    for (int i = 0; i < nsess; ++i)
        sessions.push_back(dedicated_session(conn, mtx));

    const auto t0 = clock_type::now();
    auto [ec, per_session] = co_await capy::when_all(std::move(sessions));
    const auto t1 = clock_type::now();
    die_on_error("session", {ec});

    // Merge the per-session samples
    std::vector<double> latencies;
    latencies.reserve(static_cast<std::size_t>(nsess) * nqueries);
    for (const auto& s : per_session)
        latencies.insert(latencies.end(), s.begin(), s.end());

    print_results(std::chrono::duration<double>(t1 - t0).count(), latencies);
}

static capy::task<> co_main()
{
    co_connection conn{co_await capy::this_coro::executor};
    diagnostics diag;

    // Establishing the connection is not part of the measurement
    auto [ec] = co_await conn.connect(make_connect_params(), &diag);
    die_on_error("connect", {ec, diag});

    co_await run_dedicated(conn);
}

int main()
{
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
    )(co_main());

    // Executes all pending work, including the main coroutine
    ctx.run();
}
