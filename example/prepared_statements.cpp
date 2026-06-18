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
#include <string_view>
#include <vector>

#include "nativepg/co_connection.hpp"
#include "nativepg/extended_error.hpp"
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

    // Statement objects remember the parameters (compile-time, for diagnosis)
    // and a user-supplied name. The name can be anything, but must be unique
    // within this session. Used by the server to identify the statement when executing it.
    // A good choice is to perform statement preparation once at program startup and reuse them.
    statement<std::string_view, int> insert_stmt{"insert"};
    statement<std::string_view> select_stmt{"select"};

    // Compose the request to prepare these statements
    request req{false};  // Turns off autosync for better efficiency
    req.add_prepare("INSERT INTO myt (f1, f3) VALUES ($1, $2)", insert_stmt);
    req.add_prepare("SELECT * FROM myt WHERE f1 = $1", select_stmt);
    req.add_sync();

    // Actually prepare the statements
    if (auto [ec] = co_await conn.exec(req, &diag); ec)
    {
        print_err("Error preparing", ec, diag);
        co_return;
    }

    // Now execute one of the statements.
    // Note that statements are independent even if you prepared them together
    req = request();  // TODO: replace by clear when we have it
    req.add_execute(select_stmt.bind("hola"));
    req.add_sync();

    std::vector<myrow> rows;
    response resp{into(rows)};
    if (auto [ec] = co_await conn.exec(req, resp, &diag); ec)
    {
        print_err("Error executing", ec, diag);
        co_return;
    }

    for (const auto& r : rows)
        std::cout << "Got row: " << r.f1 << ", " << r.f3 << std::endl;

    // If you happen to be preparing statements dynamically,
    // you can close them like follows. Closing the connection also deallocates
    // statements, so this is usually unnecessary.
    req = request();  // TODO: replace by clear when we have it
    req.add_close_statement(insert_stmt.name);
    req.add_close_statement(select_stmt.name);
    req.add_sync();
    if (auto [ec] = co_await conn.exec(req, &diag); ec)
    {
        print_err("Error closing", ec, diag);
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
