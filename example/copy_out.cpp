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
#include <boost/variant2/variant.hpp>

#include <iostream>

#include "nativepg/co_connection.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/request.hpp"
#include "nativepg/response.hpp"

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
    auto [ec] = co_await conn.connect(
        {.hostname = "localhost", .username = "postgres", .password = "secret", .database = "postgres"},
        &diag
    );
    if (ec)
    {
        print_err("Error connecting", ec, diag);
        co_return;
    }
    std::cout << "Startup complete\n";

    // Compose our request
    request req;
    req.add_simple_query("COPY myt TO STDOUT");

    // A response type that verifies that the server sends no error
    check check_response;

    // Setup the request for exec_some
    // Important: req and the handler must be kept alive until we finish executing
    conn.setup_request(req, check_response);

    bool done = false;
    while (!done)
    {
        auto [ec2, res] = co_await conn.exec_some();
        if (ec2)
        {
            print_err("Error reading response", ec2, {});
            co_return;
        }

        switch (res.type())
        {
            case exec_some_result::kind::done:
            {
                done = true;
                break;
            };
            case exec_some_result::kind::copy_out:
            {
                std::cout << "Initiating COPY of " << res.get_copy_out().fmt_codes.size() << " fields\n";
                break;
            }
            case exec_some_result::kind::copy_out_data:
            {
                for (auto buff : res.get_copy_out_data())
                    std::cout.write(static_cast<const char*>(buff.data()), buff.size());
                if (res.get_copy_out_eof())
                {
                    std::cout << "Copy done\n";
                }
                break;
            }
        }
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
