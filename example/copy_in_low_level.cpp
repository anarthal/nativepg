//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/capy/buffers.hpp>
#include <boost/capy/buffers/make_buffer.hpp>
#include <boost/capy/error.hpp>
#include <boost/capy/ex/async_event.hpp>
#include <boost/capy/ex/run_async.hpp>
#include <boost/capy/io_task.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/when_all.hpp>
#include <boost/capy/write.hpp>
#include <boost/corosio/io_context.hpp>
#include <boost/describe/class.hpp>

#include <cstddef>
#include <iostream>
#include <string_view>

#include "nativepg/co_connection.hpp"
#include "nativepg/exclusivity.hpp"
#include "nativepg/exec_state.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/request.hpp"
#include "nativepg/responses/check.hpp"
#include "nativepg/responses/error_into.hpp"
#include "nativepg/write_status.hpp"

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
    req.add_query("COPY myt FROM STDIN");
    extended_error err;
    error_into handler(check(), err);

    // Register the request
    exec_state exec_st;
    if (auto ec = conn.prepare_request(exec_st, req, &handler, exclusivity::exclusive))
    {
        print_err("Error preparing the request", ec, diag);
        co_return;
    }

    // Write the request
    if (auto [ec] = co_await conn.write_request(exec_st); ec)
    {
        print_err("Error writing the request", ec, diag);
        co_return;
    }

    capy::async_event copy_in_received;

    auto [ec, dummy1, dummy2] = co_await capy::when_all(
        [&]() -> capy::io_task<> {
            // Writer. Wait for the server to send
            // CopyInResponse and thus enter Copy-in mode
            if (auto [ec] = co_await copy_in_received.wait(); ec)
                co_return {ec};

            // Write the data
            // TODO: extract this from somewhere useful
            constexpr std::string_view copy_data = "hello\t42\nworld\t50\n";
            std::size_t written = 0u;
            while (written < copy_data.size())
            {
                const auto buff = capy::make_buffer(copy_data.substr(written));
                auto [ec, bytes] = co_await conn.write_some_copy_data(exec_st, buff);
                if (ec)
                    co_return {ec};
                written += bytes;
            }

            // Tell the server that we're done
            co_return co_await conn.write_copy_done(exec_st);
        }(),
        [&]() -> capy::io_task<> {
            while (!exec_st.read_done())
            {
                // Read part of the response
                if (auto [ec] = co_await conn.read_some_response(exec_st); ec)
                    co_return {ec};

                // If we're in Copy-in mode, notify the writer
                if (exec_st.write_phase() == write_status::copy_data)
                    copy_in_received.set();
            }

            // TODO: we could try to detect server-issued errors
            // (but better in a high-level utility)
            co_return {};
        }()
    );

    if (ec && ec != err.code)
    {
        print_err("Error executing the request", ec, {});
        co_return;
    }
    else if (err.code)
    {
        print_err("Handler returned an error", err.code, err.diag);
        co_return;
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
