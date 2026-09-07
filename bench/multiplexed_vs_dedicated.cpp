//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

/**
 * Compares latency and throughput of simple SELECT queries run through a
 * dedicated connection vs. a multiplexed one.
 *
 * Both benchmarks have the same shape: nsess sessions run concurrently, and
 * each session runs nqueries SELECT queries one after another. They differ
 * only in how the sessions reach the server:
 *   - Dedicated: all sessions share a single co_connection. Since a dedicated
 *     connection can only serve one request at a time, access to it is
 *     serialized with a capy::async_mutex.
 *   - Multiplexed: all sessions share a single co_multiplexed_connection,
 *     which accepts concurrent requests and pipelines them itself, so no
 *     mutex is needed.
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

#include <boost/assert/source_location.hpp>
#include <boost/capy/ex/async_mutex.hpp>
#include <boost/capy/ex/executor_ref.hpp>
#include <boost/capy/ex/run_async.hpp>
#include <boost/capy/ex/this_coro.hpp>
#include <boost/capy/io_task.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/when_all.hpp>
#include <boost/capy/when_any.hpp>
#include <boost/corosio/io_context.hpp>

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string_view>
#include <system_error>
#include <variant>
#include <vector>

#include "nativepg/co_connection.hpp"
#include "nativepg/co_multiplexed_connection.hpp"
#include "nativepg/request.hpp"
#include "nativepg/responses/check.hpp"

using namespace nativepg;
namespace capy = boost::capy;
namespace corosio = boost::corosio;

using clock_type = std::chrono::steady_clock;

namespace {

// Benchmark parameters
constexpr std::string_view query = "SELECT first_name FROM employee WHERE id = $1";
constexpr int nqueries = 1000;
constexpr int nsess = 10;

connect_params make_connect_params()
{
    return {
        .hostname = "localhost",
        .username = "postgres",
        .password = "secret",
        .database = "postgres",
    };
}

// The benchmark has nothing meaningful to report if any operation fails,
// so bail out as soon as one does.
[[noreturn]] void die(
    const char* prefix,
    std::error_code ec,
    boost::source_location loc = BOOST_CURRENT_LOCATION
)
{
    std::cerr << prefix << ": " << ec << ": " << ec.message() << "\n"
              << "  Called from " << loc << std::endl;
    std::exit(1);
}

// Latency accumulator, in microseconds. Samples are folded in as they are
// produced (Welford's online algorithm), so we never store them.
class stats
{
    std::size_t count_{};
    double mean_{};
    double m2_{};  // sum of squared deviations from the running mean

public:
    void add(double sample)
    {
        ++count_;
        const double delta = sample - mean_;
        mean_ += delta / static_cast<double>(count_);
        m2_ += delta * (sample - mean_);
    }

    std::size_t count() const { return count_; }
    double mean() const { return mean_; }

    // Sample standard deviation
    double stddev() const { return count_ < 2u ? 0.0 : std::sqrt(m2_ / static_cast<double>(count_ - 1u)); }
};

struct dedicated_state
{
    co_connection conn;
    capy::async_mutex mtx;
    stats latency;  // shared by all sessions; they all run on the same executor

    explicit dedicated_state(capy::executor_ref ex) : conn(ex) {}
};

// Runs nqueries queries serially, taking the mutex around each one, and folds
// each exec() latency into the shared accumulator.
capy::io_task<> dedicated_session(dedicated_state& st, std::int64_t session_id)
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

void print_results(const char* name, double elapsed_secs, const stats& latency)
{
    const auto total_queries = static_cast<double>(latency.count());

    std::cout << std::fixed << std::setprecision(2)                                        //
              << "\n=== " << name << " ===\n"                                              //
              << "Sessions:            " << nsess << '\n'                                  //
              << "Queries per session: " << nqueries << '\n'                               //
              << "Queries run:         " << latency.count() << '\n'                        //
              << "\nThroughput\n"                                                          //
              << "  total time:        " << elapsed_secs * 1e3 << " ms\n"                  //
              << "  time per query:    " << elapsed_secs * 1e6 / total_queries << " us\n"  //
              << "  queries/second:    " << total_queries / elapsed_secs << '\n'           //
              << "\nQuery latency (us)\n"                                                  //
              << "  mean:              " << latency.mean() << '\n'                         //
              << "  stddev:            " << latency.stddev() << '\n';
}

// Runs all the sessions against a single connection and reports the results.
// The connection must already be established.
capy::task<> run_dedicated()
{
    // Setup
    dedicated_state st{co_await capy::this_coro::executor};

    // Establishing the connection is not part of the measurement
    if (auto [ec] = co_await st.conn.connect(make_connect_params()); ec)
        die("connect", ec);

    // Discard one query, so that first-query costs don't land in the
    // measurement (the multiplexed benchmark does the same)
    request warmup;
    warmup.add_query(query, -1);
    if (auto [ec] = co_await st.conn.exec(warmup, check_execute()); ec)
        die("warmup", ec);

    // Tasks are lazy: none of these run until when_all awaits them
    std::vector<capy::io_task<>> sessions;
    sessions.reserve(nsess);
    for (int i = 0; i < nsess; ++i)
        sessions.push_back(dedicated_session(st, i));

    const auto t0 = clock_type::now();
    if (auto [ec] = co_await capy::when_all(std::move(sessions)); ec)
        die("session", ec);
    const auto t1 = clock_type::now();

    print_results("Dedicated connection", std::chrono::duration<double>(t1 - t0).count(), st.latency);
}

struct multiplexed_state
{
    co_multiplexed_connection conn;
    stats latency;  // shared by all sessions; they all run on the same executor

    explicit multiplexed_state(capy::executor_ref ex) : conn(ex) {}
};

// Runs nqueries queries serially. No mutex here: a multiplexed connection
// accepts concurrent requests and pipelines them itself.
capy::io_task<> multiplexed_session(multiplexed_state& st, std::int64_t session_id)
{
    // Serializing the request once and reusing it keeps request composition
    // out of the measurement.
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

// Runs all the sessions and reports the results. Must run concurrently with
// conn.run(), which is what actually drives the connection.
capy::io_task<> multiplexed_bench(multiplexed_state& st)
{
    // A multiplexed connection has no separate connect step: exec() blocks
    // until run() has established the session. Pay that cost with a single
    // warm-up query, before the clock starts.
    request warmup;
    warmup.add_query(query, -1);
    if (auto [ec] = co_await st.conn.exec(warmup, check_execute()); ec)
        die("warmup", ec);

    // Tasks are lazy: none of these run until when_all awaits them
    std::vector<capy::io_task<>> sessions;
    sessions.reserve(nsess);
    for (int i = 0; i < nsess; ++i)
        sessions.push_back(multiplexed_session(st, i));

    const auto t0 = clock_type::now();
    if (auto [ec] = co_await capy::when_all(std::move(sessions)); ec)
        die("session", ec);
    const auto t1 = clock_type::now();

    print_results("Multiplexed connection", std::chrono::duration<double>(t1 - t0).count(), st.latency);

    co_return {};
}

capy::task<> run_multiplexed()
{
    // Setup
    multiplexed_state st{co_await capy::this_coro::executor};
    multiplexed_config cfg{.transport = make_connect_params()};

    // run() only returns on error, so race it against the benchmark: once the
    // benchmark wins, when_any stop-requests run() and waits for it to unwind.
    auto res = co_await capy::when_any(st.conn.run(std::move(cfg)), multiplexed_bench(st));
    if (res.index() == 0)
        die("multiplexed", std::get<0>(res));
}

capy::task<> co_main()
{
    co_await run_dedicated();
    co_await run_multiplexed();
}

}  // namespace

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
