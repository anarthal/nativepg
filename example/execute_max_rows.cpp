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
#include <string_view>
#include <vector>

#include "nativepg/co_connection.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/protocol/execute.hpp"
#include "nativepg/request.hpp"
#include "nativepg/response.hpp"

using namespace nativepg;
namespace capy = boost::capy;
namespace corosio = boost::corosio;

struct myrow
{
    std::int32_t f3;
    std::string f1;
};
BOOST_DESCRIBE_STRUCT(myrow, (), (f3, f1))

static void print_err(const char* prefix, std::error_code err, const diagnostics& diag)
{
    std::cout << prefix << ": " << err << ": " << err.message();
    if (!diag.message().empty())
        std::cout << ": " << diag.message();
    std::cout << '\n';
}

static void print_rows(std::span<const myrow> rows)
{
    std::cout << "Row batch of size: " << rows.size() << '\n';
    for (const auto& row : rows)
        std::cout << "{ .f3=" << row.f3 << ", .f1=" << row.f1 << " }\n";
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

    // Initial execution
    request req_initial{false};  // disable auto-sync
    statement<std::string_view> stmt{};
    req_initial.add_query("BEGIN", {})
        .add_prepare("SELECT * FROM myt WHERE f1 <> $1", stmt)
        .add_bind(stmt.bind("abc"), request::param_format::select_best, "")
        .add_describe_portal("")
        .add(protocol::execute{.portal_name = "", .max_num_rows = 2})
        .add_sync();

    // Subsequent executions
    request req_subsequent{false};
    req_subsequent.add_describe_portal("")
        .add(protocol::execute{.portal_name = "", .max_num_rows = 2})
        .add_sync();

    // Cleanup
    request req_final;  // with autosync
    req_final.add_query("COMMIT", {});

    // Start execution
    std::vector<myrow> rows;
    command_info info;

    if (auto [ec] = co_await conn.exec(req_initial, response{check_execute(), into(rows, &info)}, &diag); ec)
    {
        print_err("Error executing the initial request", ec, diag);
        co_return;
    }

    print_rows(rows);

    // Read rows until the portal is exhausted
    while (info.portal_suspended)
    {
        rows.clear();
        if (auto [ec] = co_await conn.exec(req_subsequent, into(rows, &info), &diag); ec)
        {
            print_err("Error executing the follow up request", ec, diag);
            co_return;
        }

        print_rows(rows);
    }

    // Cleanup
    if (auto [ec] = co_await conn.exec(req_final, check(), &diag); ec)
    {
        print_err("Error during cleanup", ec, diag);
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
