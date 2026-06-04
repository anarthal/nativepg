//
// Copyright (c) 2019-2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

/**
 * This example demonstrates how to use co_connection_pool together with
 * corosio's tcp_server to implement a server for a simple custom TCP-based
 * protocol.
 *
 * The protocol can be used to retrieve the full name of an employee given
 * their ID. It works as follows:
 *   - The client connects.
 *   - The client sends the employee ID as a big-endian 64-bit signed int.
 *   - The server responds with a string containing the employee's full name.
 *   - The connection is closed.
 *
 * It uses Boost.Capy for coroutines, Boost.Corosio for the I/O primitives,
 * and nativepg's co_connection_pool to manage Postgres connections.
 *
 * tcp_server owns a fixed pool of worker objects, dispatches an idle worker
 * for each accepted connection, and returns the worker to the pool when the
 * handling coroutine completes. This avoids fire-and-forget coroutines that
 * could outlive the accept loop.
 *
 * This example expects a Postgres database with an `employee` table:
 *
 *     CREATE TABLE employee (
 *         id          BIGINT PRIMARY KEY,
 *         first_name  TEXT NOT NULL,
 *         last_name   TEXT NOT NULL
 *     );
 */

#include <boost/capy/buffers/make_buffer.hpp>
#include <boost/capy/ex/run_async.hpp>
#include <boost/capy/io_task.hpp>
#include <boost/capy/read.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/timeout.hpp>
#include <boost/capy/write.hpp>
#include <boost/corosio/endpoint.hpp>
#include <boost/corosio/io_context.hpp>
#include <boost/corosio/shutdown_type.hpp>
#include <boost/corosio/signal_set.hpp>
#include <boost/corosio/tcp_server.hpp>
#include <boost/corosio/tcp_socket.hpp>
#include <boost/describe/class.hpp>
#include <boost/endian/conversion.hpp>

#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

#include "nativepg/co_connection_pool.hpp"
#include "nativepg/request.hpp"
#include "nativepg/response.hpp"

namespace capy = boost::capy;
namespace corosio = boost::corosio;
using namespace nativepg;
using namespace std::chrono_literals;

// One row of the query result. Boost.Describe lets the row callback
// populate the fields by name.
struct employee
{
    std::string first_name;
    std::string last_name;
};
BOOST_DESCRIBE_STRUCT(employee, (), (first_name, last_name))

// Encapsulates the database access logic.
// Given an employee_id, retrieves the employee details to be sent to the client.
static capy::io_task<std::string> get_employee_details(co_connection_pool& pool, std::int64_t employee_id)
{
    // Get a connection from the pool, with a timeout. pooled_connection grants
    // exclusive access to the connection until destroyed.
    auto [ec_get, pconn] = co_await capy::timeout(pool.get_connection(), 20s);
    if (ec_get)
        co_return {ec_get, {}};

    // Build the query. Positional parameters in PostgreSQL use $N.
    request req;
    req.add_query("SELECT first_name, last_name FROM employee WHERE id = $1", {employee_id});

    std::vector<employee> rows;
    response res{into(rows)};

    auto [ec_exec] = co_await pconn->exec(req, res);
    if (ec_exec)
        co_return {ec_exec, {}};

    if (rows.empty())
        co_return {{}, std::string{"NOT_FOUND"}};
    co_return {{}, rows[0].first_name + ' ' + rows[0].last_name};
}

static capy::task<> handle_session(corosio::tcp_socket& sock, co_connection_pool& pool)
{
    // Read the 8-byte request. capy::read fills the whole buffer or fails.
    unsigned char buf[8]{};
    if (auto [ec, n] = co_await capy::read(sock, capy::make_buffer(buf)); ec)
    {
        std::cerr << "Error reading from client: " << ec << '\n';
        co_return;
    }

    const std::int64_t employee_id = boost::endian::load_big_s64(buf);

    // Look it up in the DB.
    auto [ec_db, response_text] = co_await get_employee_details(pool, employee_id);
    if (ec_db)
    {
        std::cerr << "Error querying the database: " << ec_db << '\n';
        response_text = "ERROR";
    }

    // Write the response back. capy::write writes the whole buffer or fails.
    if (auto [ec, n] = co_await capy::write(sock, capy::make_buffer(response_text)); ec)
    {
        std::cerr << "Error writing to the client: " << ec << '\n';
        co_return;
    }

    // Cleanly close the connection. tcp_server will re-initialize the
    // socket on the next accept dispatched to this worker.
    sock.shutdown(corosio::shutdown_both);
    sock.close();
    co_return;
}

// One session worker, recycled by tcp_server across many connections.
// tcp_server holds the workers, accepts into worker.socket(), then calls
// worker.run(launcher); when the launched coroutine completes, the worker
// goes back to the idle pool.
class session_worker final : public corosio::tcp_server::worker_base
{
    corosio::tcp_socket sock_;
    corosio::io_context* ctx_;
    co_connection_pool* pool_;

public:
    session_worker(corosio::io_context& ctx, co_connection_pool& pool) : sock_(ctx), ctx_(&ctx), pool_(&pool)
    {
    }

    corosio::tcp_socket& socket() override { return sock_; }

    void run(corosio::tcp_server::launcher launch) override
    {
        launch(ctx_->get_executor(), [this]() -> capy::task<> { return handle_session(sock_, *pool_); }());
    }
};

static std::vector<std::unique_ptr<corosio::tcp_server::worker_base>> make_workers(
    corosio::io_context& ctx,
    co_connection_pool& pool,
    std::size_t n
)
{
    std::vector<std::unique_ptr<corosio::tcp_server::worker_base>> v;
    v.reserve(n);
    for (std::size_t i = 0; i < n; ++i)
        v.push_back(std::make_unique<session_worker>(ctx, pool));
    return v;
}

int main(int argc, char** argv)
{
    if (argc != 5)
    {
        std::cerr << "Usage: " << argv[0] << " <username> <password> <server-hostname> <listener-port>\n";
        return 1;
    }

    const char* username = argv[1];
    const char* password = argv[2];
    const char* server_hostname = argv[3];
    const auto listener_port = static_cast<std::uint16_t>(std::stoi(argv[4]));

    // The I/O context.
    corosio::io_context ctx;
    auto ex = ctx.get_executor();

    // The connection pool. It must be kept alive for the lifetime of the application.
    // clang-format off
    co_connection_pool pool(ex, {
        .transport = {
            .hostname = server_hostname,
            .username = username,
            .password = password,
            .database = "nativepg_examples",
        }
    });
    // clang-format on

    // co_connection_pool::run() must be called exactly once.
    // This stop source will be signaled when the user hits Ctrl+C
    std::stop_source pool_stop;
    capy::run_async(ex, pool_stop.get_token())(pool.run());

    // The TCP server. set_workers caps the number of concurrent sessions.
    corosio::tcp_server srv(ctx, ex);
    srv.set_workers(make_workers(ctx, pool, 256));
    if (auto ec = srv.bind(corosio::endpoint(listener_port)))
    {
        std::cerr << "Bind failed: " << ec << '\n';
        return 1;
    }
    srv.start();
    std::cout << "Server listening on port " << srv.local_endpoint().port() << '\n';

    // Wait for SIGINT/SIGTERM and trigger a cooperative shutdown
    corosio::signal_set signals(ctx, SIGINT, SIGTERM);
    capy::run_async(ex)(
        [](corosio::signal_set& sigs, corosio::tcp_server& server, std::stop_source& src) -> capy::task<> {
            auto [ec, signum] = co_await sigs.wait();
            if (!ec)
                std::cout << "Received signal " << signum << ", shutting down\n";
            server.stop();       // stop accepting new connections, drain active workers
            src.request_stop();  // unblock pool.run() so connections shut down
        }(signals, srv, pool_stop)
    );

    // Drain the context. Returns once the pool, the server's workers and
    // accept loops, and the signal handler have all finished.
    ctx.run();

    // Final join from a thread NOT running ctx.run() (we are now). Waits for
    // any lingering accept-loop tasks to fully exit before destruction.
    srv.join();
}
