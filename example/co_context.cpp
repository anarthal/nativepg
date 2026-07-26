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
#include "nativepg/extended_error.hpp"
#include "nativepg/response.hpp"
#include "nativepg/context/db_info.hpp"
#include "nativepg/context/type_info.hpp"

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

static void print_err(const char* prefix, const extended_error& err)
{
    print_err(prefix, err.code, err.diag);
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

    auto [ec_ctx] = co_await conn.load_contexts<context::db_info_tag, context::type_info_tag>(&diag);
    if (ec_ctx)
    {
        print_err("Error loading contexts", ec_ctx, diag);
        co_return;
    }

    auto db_info = context::make_context<context::db_info_tag>();
    if (!conn.context().resolve<context::db_info_tag>(db_info))
    {
        std::cout << "Server version: " << db_info.server_version << std::endl;
    }

    auto types_info = context::make_context<context::type_info_tag>();
    if (!conn.context().resolve<context::type_info_tag>(types_info))
        for (const auto& type_info : types_info)
    {
        std::cout << "Type: " << type_info.type_name << ", OID: " << type_info.type_oid << std::endl;
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
