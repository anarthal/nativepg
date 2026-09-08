//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

/**
 * Does opening several multiplexed connections gain performance?
 * See bench/README.md for a rationale.
 *
 * The workload is the same as in multiplexed_vs_dedicated.cpp: a fixed number
 * of sessions, each issuing a fixed number of simple SELECTs. The only thing
 * that changes between cases is how many multiplexed connections the sessions
 * are spread over:
 *   - Case A: a single connection, shared by all sessions.
 *   - Case B: two connections; half the sessions use one, half the other.
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
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "bench_utils.hpp"
#include "nativepg/co_multiplexed_connection.hpp"
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

struct multiplexed_state
{
    std::vector<co_multiplexed_connection> conns;
    stats latency;  // shared by all sessions; they all run on the same executor

    multiplexed_state(capy::executor_ref ex, std::size_t nconns)
    {
        // run() holds a reference to each connection, so the vector must not
        // reallocate once the benchmark is under way
        conns.reserve(nconns);
        for (std::size_t i = 0; i < nconns; ++i)
            conns.emplace_back(ex);
    }
};

capy::io_task<> multiplexed_session(multiplexed_state& st, std::int64_t session_id)
{
    // Sessions are spread over the available connections round-robin
    auto& conn = st.conns[static_cast<std::size_t>(session_id) % st.conns.size()];

    // Serializing the request once and reusing it keeps request composition
    // out of the measurement.
    request req;
    req.add_query(query, session_id);

    for (int i = 0; i < nqueries; ++i)
    {
        const auto t1 = clock_type::now();

        if (auto [ec] = co_await conn.exec(req, check_execute()); ec)
            die("execute", ec);

        const auto t2 = clock_type::now();

        st.latency.add(std::chrono::duration<double, std::micro>(t2 - t1).count());
    }

    co_return {};
}

// Runs all the sessions and reports the results. Must run concurrently with
// run() on every connection, which is what actually drives them.
capy::io_task<> multiplexed_bench(multiplexed_state& st, const char* name)
{
    // A multiplexed connection has no separate connect step: exec() blocks
    // until run() has established the session. Pay that cost with a single
    // warm-up query per connection, before the clock starts.
    request warmup;
    warmup.add_query(query, static_cast<std::int64_t>(-1));
    for (auto& conn : st.conns)
    {
        if (auto [ec] = co_await conn.exec(warmup, check_execute()); ec)
            die("warmup", ec);
    }

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
        name,
        st.conns.size(),
        nsess,
        nqueries,
        std::chrono::duration<double>(t1 - t0).count(),
        st.latency
    );

    co_return {};
}

capy::task<> run_case(const connect_params& params, std::size_t nconns)
{
    // Setup
    multiplexed_state st{co_await capy::this_coro::executor, nconns};

    // run() only returns on error, so race it against the benchmark: once the
    // benchmark wins, when_any stop-requests the run() tasks and waits for
    // them to unwind.
    std::vector<capy::io_task<>> tasks;
    tasks.reserve(nconns + 1u);
    for (auto& conn : st.conns)
        tasks.push_back(conn.run(multiplexed_config{.transport = params}));
    auto name = std::to_string(nconns) + " multiplexed connection(s)";
    tasks.push_back(multiplexed_bench(st, name.c_str()));

    auto res = co_await capy::when_any(std::move(tasks));
    if (res.index() == 0)
        die("multiplexed", std::get<0>(res));
}

// params must outlive this coroutine
capy::task<> co_main(const connect_params& params)
{
    for (std::size_t i = 1u; i < 16u; ++i)
        co_await run_case(params, i);
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
