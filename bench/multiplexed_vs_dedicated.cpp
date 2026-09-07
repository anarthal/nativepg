//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/assert/source_location.hpp>
#include <boost/capy/ex/async_mutex.hpp>
#include <boost/capy/ex/run_async.hpp>
#include <boost/capy/task.hpp>
#include <boost/corosio/io_context.hpp>
#include <boost/describe/class.hpp>

#include <chrono>
#include <iostream>
#include <string_view>
#include <vector>

#include "nativepg/co_connection.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/request.hpp"
#include "nativepg/responses/check.hpp"
#include "nativepg/responses/error_into.hpp"
#include "nativepg/responses/into.hpp"
#include "nativepg/responses/response.hpp"

using namespace nativepg;
namespace capy = boost::capy;
namespace corosio = boost::corosio;

struct myrow
{
    std::string f1;
    std::int32_t f3;
};
BOOST_DESCRIBE_STRUCT(myrow, (), (f1, f3))

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

static void die_on_error(
    const char* prefix,
    const extended_error& err,
    boost::source_location loc = BOOST_CURRENT_LOCATION
);

// Accumulator
struct stats
{
    double avg;
    // TODO: add whatever values we need
};

constexpr std::string_view query = "SELECT first_name FROM employee WHERE id = $1";
constexpr int nqueries = 1000;
constexpr int nsess = 10;

static capy::io_task<stats> dedicated_session()
{
    // Create a connection
    co_connection conn{co_await capy::this_coro::executor};
    diagnostics diag;
    stats st;
    capy::async_mutex mtx;

    // Connect
    auto [ec] = co_await conn.connect(
        {.hostname = "localhost", .username = "postgres", .password = "secret", .database = "postgres"},
        &diag
    );
    die_on_error("connect", {ec, diag});

    // Setup the request
    request req;
    req.add_query(query, 42);

    //
    for (int i = 0; i < nqueries; ++i)
    {
        auto t1 = std::chrono::steady_clock::now();
        auto [ec2] = co_await conn.exec(req, check_execute(), &diag);
        die_on_error("execute", {ec2, diag});
        auto t2 = std::chrono::steady_clock::now();
        // TODO: add to stats
    }

    co_return {{}, st};
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
