//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/capy/ex/run_async.hpp>
#include <boost/capy/task.hpp>
#include <boost/corosio/io_context.hpp>

#include <iostream>

#include "nativepg/co_connection.hpp"
#include "nativepg/dynamic_resultset.hpp"
#include "nativepg/extended_error.hpp"
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
    req.add_query("SELECT * FROM myt WHERE f1 <> 'abc'", {});

    // Structures to parse the response into
    dynamic_resultset res;
    dynamic_resultset_response handler{res};

    auto [ec2] = co_await conn.exec(req, handler, &diag);
    if (ec2)
    {
        print_err("Operation result", ec2, diag);
        co_return;
    }

    // Print the columns
    for (const auto col : res.field_descriptions())
        std::cout << col.name << " | ";
    std::cout << '\n';

    // Print the rows
    for (const auto row : res.rows())
    {
        for (const auto value : row)
        {
            std::cout << (value.is_null() ? "<NULL>" : value.data_str()) << " | ";
        }
        std::cout << '\n';
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
