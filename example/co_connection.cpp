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

#include "nativepg/co_connection.hpp"
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

static void print_err(const char* prefix, std::error_code err)
{
    std::cout << prefix << err << ": " << err.message() << '\n';
}

static void print_err(const char* prefix, const extended_error& err)
{
    std::cout << prefix << err.code.what() << ": " << err.diag.message() << '\n';
}

static capy::task<> co_main()
{
    // Create a connection
    co_connection conn{co_await capy::this_coro::executor};

    // Connect
    auto [ec] = co_await conn.connect(
        {.hostname = "localhost", .username = "postgres", .password = "secret", .database = "postgres"}
    );
    if (ec)
    {
        print_err("Error connecting", ec);
        co_return;
    }
    std::cout << "Startup complete\n";

    // Compose our request
    request req;
    req.add_query("SELECT * FROM myt' WHERE f1 <> $1", {"abc"});
    req.add_query("SELECT * FROM myt WHERE f1 <> 'abc'", {});

    // Structures to parse the response into
    std::vector<myrow> vec1, vec2;
    response res{into(vec1), into(vec2)};

    auto [ec2] = co_await conn.exec(req, res);
    print_err("Operation result: ", ec2);
    print_err("Q1 result: ", std::get<0>(res.handlers()).result());
    print_err("Q2 result: ", std::get<1>(res.handlers()).result());

    for (const auto& r : vec1)
        std::cout << "Got row (1): " << r.f1 << ", " << r.f3 << std::endl;
    for (const auto& r : vec2)
        std::cout << "Got row (2): " << r.f1 << ", " << r.f3 << std::endl;

    std::cout << "Done\n";
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
      })(co_main());

    // Executes all pending work, including the main coroutine
    ctx.run();
}
