//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/capy/cond.hpp>
#include <boost/capy/delay.hpp>
#include <boost/capy/ex/run_async.hpp>
#include <boost/capy/ex/this_coro.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/when_any.hpp>
#include <boost/corosio/io_context.hpp>

#include <iostream>

#include "nativepg/co_connection.hpp"
#include "nativepg/connect_params.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/request.hpp"
#include "nativepg/responses/check.hpp"

using namespace nativepg;
namespace capy = boost::capy;
namespace corosio = boost::corosio;
using namespace std::chrono_literals;

static void print_err(const char* prefix, std::error_code err, const diagnostics& diag)
{
    std::cout << prefix << ": " << err << ": " << err.message();
    if (!diag.message().empty())
        std::cout << ": " << diag.message();
    std::cout << '\n';
}

static capy::io_task<> read_notifications(co_connection& conn)
{
    while (true)
    {
        // Wait for notifications
        auto [ec, notifications] = co_await conn.receive();
        if (ec)
            co_return {ec};

        // Act on the notifications
        for (const auto& notif : notifications)
        {
            std::cout << "Received notification from channel = '" << notif.channel_name << "', payload = '"
                      << notif.payload << "'\n";
        }
    }
}

static capy::task<> co_main()
{
    // Create a connection
    co_connection conn{co_await capy::this_coro::executor};

    request req;
    req.add_simple_query("LISTEN \"mychannel\"");

    connect_params conn_params{
        .hostname = "localhost",
        .username = "postgres",
        .password = "secret",
        .database = "postgres",
    };

    diagnostics diag;

    auto stop_tok = co_await capy::this_coro::stop_token;

    while (true)
    {
        // Establish the connection
        if (auto [connect_ec] = co_await conn.connect(conn_params, &diag); connect_ec)
        {
            print_err("Error establishing the connection", connect_ec, diag);
            if (stop_tok.stop_requested())
                co_return;
            if (auto [wait_ec] = co_await capy::delay(1s); wait_ec)
                co_return;
            continue;
        }

        // Subscribe to the channels of interest
        if (auto [exec_ec] = co_await conn.exec(req, check(), &diag); exec_ec)
        {
            print_err("Error issuing the LISTEN command", exec_ec, diag);
            continue;
        }

        // Read notifications
        if (auto [notif_ec] = co_await read_notifications(conn); notif_ec)
        {
            print_err("Error receiving notifications", notif_ec, {});
            continue;
        }
    }

    // TODO: can we manage to shutdown this?
}

// TODO: signals

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
