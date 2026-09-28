//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/capy/ex/run_async.hpp>
#include <boost/capy/task.hpp>
#include <boost/corosio/io_context.hpp>
#include <boost/describe/class.hpp>

#include <iostream>
#include <span>
#include <vector>

#include "nativepg/co_connection.hpp"
#include "nativepg/exec_state.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/protocol/copy.hpp"
#include "nativepg/request.hpp"
#include "nativepg/responses/copy_out_handler.hpp"

using namespace nativepg;
namespace capy = boost::capy;
namespace corosio = boost::corosio;

static void print_err(const char* prefix, std::error_code err, const diagnostics& diag)
{
    std::cout << prefix << ": " << err << ": " << err.message();
    if (!diag.message().empty())
        std::cout << ": " << diag.message();
    std::cout << '\n';
}

static capy::task<> co_main()
{
    // Create a connection
    co_connection conn{co_await capy::this_coro::executor};
    diagnostics diag;

    // Connect
    if (auto [ec] = co_await conn.connect(
            {.hostname = "localhost", .username = "postgres", .password = "secret", .database = "postgres"},
            &diag
        );
        ec)
    {
        print_err("Error connecting", ec, diag);
        co_return;
    }
    std::cout << "Startup complete\n";

    // Compose our request
    request req;
    req.add_query("COPY myt TO STDOUT");

    // Response
    std::vector<std::span<const unsigned char>> buffers;
    auto handler = copy_out_handler([&buffers](std::span<const unsigned char> buff) {
        buffers.push_back(buff);
    });

    // Register the request
    exec_state exec_st;
    if (auto [ec] = co_await conn.register_request(exec_st, req, &handler, &diag); ec)
    {
        print_err("Error registering the response", ec, diag);
        co_return;
    }

    // Write the request
    if (auto [ec] = co_await conn.write_request(exec_st); ec)
    {
        print_err("Error writing the request", ec, diag);
        co_return;
    }

    // Read the response until we are done
    while (!exec_st.read_done())
    {
        if (auto [ec] = co_await conn.read_some_response(exec_st); ec)
        {
            print_err("Error reading the response", ec, diag);
            co_return;
        }

        for (auto buff : buffers)
        {
            std::cout << "<COPY data> ";
            std::cout.write(reinterpret_cast<const char*>(buff.data()), buff.size());
        }
        buffers.clear();
    }

    // Orderly close the connection.
    // May report a failure if the server is gone.
    // The connection is closed anyway in that case.
    if (auto [shutdown_ec] = co_await conn.shutdown(); shutdown_ec)
    {
        print_err("Error during shutdown", shutdown_ec, {});
        co_return;
    }
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
