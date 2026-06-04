//
// Copyright (c) 2019-2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

/**
 * This example demonstrates how to use co_connection_pool to implement a
 * server for a simple custom TCP-based protocol.
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
#include <boost/capy/ex/this_coro.hpp>
#include <boost/capy/io_task.hpp>
#include <boost/capy/read.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/timeout.hpp>
#include <boost/capy/write.hpp>
#include <boost/corosio/endpoint.hpp>
#include <boost/corosio/io_context.hpp>
#include <boost/corosio/shutdown_type.hpp>
#include <boost/corosio/signal_set.hpp>
#include <boost/corosio/tcp_acceptor.hpp>
#include <boost/corosio/tcp_socket.hpp>
#include <boost/describe/class.hpp>
#include <boost/endian/conversion.hpp>

#include <csignal>
#include <cstdint>
#include <exception>
#include <iostream>
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
    // Get a connection from the pool. Fail if no connection becomes available
    // in the next 20 seconds. pooled_connection grants exclusive access to the
    // connection until the object is destroyed, at which point it returns to
    // the pool automatically.
    auto [ec_get, pconn] = co_await capy::timeout(pool.get_connection(), 20s);
    if (ec_get)
        co_return {ec_get, {}};

    // Build the query. Positional parameters in PostgreSQL use $N.
    request req;
    req.add_query("SELECT first_name, last_name FROM employee WHERE id = $1", {employee_id});

    // The row callback will append to this vector.
    std::vector<employee> rows;
    response res{into(rows)};

    auto [ec_exec] = co_await pconn->exec(req, res);
    if (ec_exec)
        co_return {ec_exec, {}};

    if (rows.empty())
        co_return {{}, std::string{"NOT_FOUND"}};

    co_return {{}, rows[0].first_name + ' ' + rows[0].last_name};
}

// Handles one client session: read the 8-byte employee ID, look it up, write the answer back.
static capy::io_task<> handle_session(co_connection_pool& pool, corosio::tcp_socket sock)
{
    // Read the 8-byte request. capy::read fills the whole buffer or fails.
    unsigned char buf[8]{};
    if (auto [ec_read, n_read] = co_await capy::read(sock, capy::make_buffer(buf)); ec_read)
    {
        std::cerr << "Error reading from client: " << ec_read << std::endl;
        co_return {ec_read};
    }

    const std::int64_t employee_id = boost::endian::load_big_s64(buf);

    // Hit the database.
    auto [ec_db, response_text] = co_await get_employee_details(pool, employee_id);
    if (ec_db)
    {
        std::cerr << "Error querying the database: " << ec_db << std::endl;
        response_text = "ERROR";
    }

    // Write the response back. capy::write writes the whole buffer or fails.
    if (auto [ec_write, n_write] = co_await capy::write(sock, capy::make_buffer(response_text)); ec_write)
    {
        std::cerr << "Error writing to the client: " << ec_write << std::endl;
        co_return {ec_write};
    }

    // Cleanly close the connection
    sock.shutdown(boost::corosio::shutdown_both);
    sock.close();

    co_return {};
}

// Accepts incoming TCP connections and spawns a handler coroutine for each one.
static capy::io_task<> listener(co_connection_pool& pool, std::uint16_t port)
{
    auto ex = co_await capy::this_coro::executor;
    const auto stop = co_await capy::this_coro::stop_token;

    // Convenience ctor: open + SO_REUSEADDR + bind + listen.
    corosio::tcp_acceptor acc(ex.context(), corosio::endpoint(port));
    std::cout << "Server listening on port " << port << '\n';

    while (true)
    {
        // Accept the next client.
        corosio::tcp_socket peer(ex.context());
        if (auto [ec] = co_await acc.accept(peer); ec)
            co_return {ec};

        // Spawn the session as a fire-and-forget task. Each session inherits the
        // listener's stop token, so cancellation cascades down. Exceptions in
        // the session are logged but don't bring down the listener.
        capy::run_async(
            ex,
            stop,
            []() {},
            [](std::exception_ptr ep) {
                try
                {
                    std::rethrow_exception(ep);
                }
                catch (const std::exception& e)
                {
                    std::cerr << "Session error: " << e.what() << '\n';
                }
            }
        )(handle_session(pool, std::move(peer)));
    }
}

int main(int argc, char** argv)
{
    if (argc != 5)
    {
        std::cerr << "Usage: " << argv[0] << " <username> <password> <server-hostname> <listener-port>\n";
        return 1;
    }

    const std::string username = argv[1];
    const std::string password = argv[2];
    const std::string server_hostname = argv[3];
    const auto listener_port = static_cast<std::uint16_t>(std::stoi(argv[4]));

    // The I/O context. Required for all I/O operations.
    corosio::io_context ctx;
    auto ex = ctx.get_executor();

    // Configuration for the pool.
    pool_params params;

    // The pool. Connections are created and kept healthy by pool.run().
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

    // Master stop source. Triggered when SIGINT/SIGTERM is received; both the
    // pool and the listener observe its token and shut down cooperatively.
    std::stop_source stop_src;

    // Start the pool. Must be called exactly once. Returns when stopped.
    capy::run_async(ex, stop_src.get_token())(pool.run());

    // Start the listener.
    capy::run_async(
        ex,
        stop_src.get_token(),
        []() {},
        [](std::exception_ptr ep) {
            try
            {
                std::rethrow_exception(ep);
            }
            catch (const std::exception& e)
            {
                std::cerr << "Listener error: " << e.what() << '\n';
            }
        }
    )(listener(pool, listener_port));

    // Wait for SIGINT/SIGTERM and request shutdown.
    corosio::signal_set signals(ctx, SIGINT, SIGTERM);
    capy::run_async(ex)([](corosio::signal_set& sigs, std::stop_source& src) -> capy::task<> {
        auto [ec, signum] = co_await sigs.wait();
        if (!ec)
            std::cout << "Received signal " << signum << ", shutting down\n";
        src.request_stop();
    }(signals, stop_src));

    // Drive everything to completion. Returns when all spawned coroutines exit.
    ctx.run();
}
