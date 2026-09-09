//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

/**
 * Does coalescing writes help performance?
 * See bench/README.md for a rationale.
 *
 * The two cases run the exact same workload as multiplexed_vs_dedicated's
 * multiplexed case (the code below is templated on the connection type, so
 * both cases share it), and differ only in how the connection writes:
 *   - Coalescing: co_multiplexed_connection copies every pending request into
 *     a single buffer and issues one write for the whole batch.
 *   - No coalescing: co_multiplexed_connection_nocoal issues one write per
 *     request, straight from the request's payload, with no copy.
 * Both still pipeline: requests are written without waiting for responses.
 * So the delta between them is the value of coalescing alone, not of
 * pipelining.
 *
 * Each case is run nreps times, alternating, because the expected effect is
 * small when the number of requests batched per write is small: comparing
 * single runs would not tell an effect apart from run-to-run noise.
 *
 * Reported figures:
 *   - Latency: wall time from issuing a query to having its response, as a
 *     session sees it.
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

#include <boost/capy/ex/executor_ref.hpp>
#include <boost/capy/ex/run_async.hpp>
#include <boost/capy/ex/this_coro.hpp>
#include <boost/capy/io_task.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/when_all.hpp>
#include <boost/capy/when_any.hpp>
#include <boost/corosio/io_context.hpp>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "bench_utils.hpp"
#include "nativepg/co_multiplexed_connection.hpp"
#include "nativepg/co_multiplexed_connection_nocoal.hpp"
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
constexpr int nqueries = 50;
constexpr int nsess = 100;

// Connection is either co_multiplexed_connection or co_multiplexed_connection_nocoal
template <class Connection>
struct multiplexed_state
{
    Connection conn;
    stats latency;  // shared by all sessions; they all run on the same executor

    explicit multiplexed_state(capy::executor_ref ex) : conn(ex) {}
};

template <class Connection>
capy::io_task<> multiplexed_session(multiplexed_state<Connection>& st, std::int64_t session_id)
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
template <class Connection>
capy::io_task<> multiplexed_bench(multiplexed_state<Connection>& st, std::string name)
{
    // A multiplexed connection has no separate connect step: exec() blocks
    // until run() has established the session. Pay that cost with a single
    // warm-up query, before the clock starts.
    request warmup;
    warmup.add_query(query, static_cast<std::int64_t>(-1));
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

    print_results(
        name.c_str(),
        1u,
        nsess,
        nqueries,
        std::chrono::duration<double>(t1 - t0).count(),
        st.latency
    );

    co_return {};
}

// Same as multiplexed_vs_dedicated's run_multiplexed, but with the connection
// type as a parameter. Each call uses a fresh connection.
template <class Connection>
capy::task<> run_multiplexed(const connect_params& params, std::string name)
{
    // Setup
    multiplexed_state<Connection> st{co_await capy::this_coro::executor};
    multiplexed_config cfg{.transport = params};

    // run() only returns on error, so race it against the benchmark: once the
    // benchmark wins, when_any stop-requests run() and waits for it to unwind.
    auto res = co_await capy::when_any(st.conn.run(std::move(cfg)), multiplexed_bench(st, std::move(name)));
    if (res.index() == 0)
        die("multiplexed", std::get<0>(res));
}

// params must outlive this coroutine
capy::task<> co_main(const connect_params& params)
{
    co_await run_multiplexed<co_multiplexed_connection>(params, "Coalescing");
    co_await run_multiplexed<co_multiplexed_connection_nocoal>(params, "No coalescing");
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
