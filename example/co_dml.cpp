//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

/*
 * Data Manipulation Language (DML) capy / corosio example
 */
#include <boost/capy/ex/run_async.hpp>
#include <boost/capy/task.hpp>
#include <boost/corosio/io_context.hpp>
#include <boost/describe/class.hpp>

#include <iostream>
#include <chrono>
#include <vector>
#include <cstdint>


#include "nativepg/co_connection.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/response.hpp"

using namespace nativepg;
namespace capy = boost::capy;
namespace corosio = boost::corosio;

struct row_count
{
    std::int64_t amount;
};
BOOST_DESCRIBE_STRUCT(row_count, (), (amount))

static void print_err(const char* prefix, const std::error_code& err, const diagnostics& diag)
{
    std::cerr << prefix << err.message() << ": " << diag.message() << '\n';
}

static capy::task<> co_main()
{
    // Timing Start...
    using clock = std::chrono::steady_clock;
    using ms = std::chrono::milliseconds;
    auto start = clock::now();

    // Create a connection
    co_connection conn{co_await capy::this_coro::executor};
    diagnostics diag;

    // Connect
    if (auto [ec] = co_await conn.connect(
        {.hostname = "localhost", .username = "postgres", .password = "secret", .database = "postgres"},
        &diag); ec)
    {
        print_err("Error connecting", ec, diag);
        co_return;
    }
    std::cout << "Startup complete\n";

    // Create
    request create_req;
    create_req.add_query("CREATE TABLE IF NOT EXISTS persons ( id bigserial primary key , name text not null, postal_code integer)", {});
    response create_res{check()};
    if (auto [create_ec] = co_await conn.exec(create_req, create_res, &diag); create_ec)
    {
        print_err("Create result: ", create_ec, diag);
        co_return;
    }
    std::cout << "Created successfully\n";

    // Insert
    request insert_req;
    insert_req.add_query("INSERT INTO persons (name, postal_code) VALUES ('Bert', 1000), ('Ernie', 2000)", {});
    response insert_res{check()};
    if (auto [insert_ec] = co_await conn.exec(insert_req, insert_res, &diag); insert_ec)
    {
        print_err("Insert result: ", insert_ec, diag);
        co_return;
    }
    std::cout << "Inserted successfully\n";

    // Update
    request update_req;
    update_req.add_query("UPDATE persons SET postal_code = $1", {3000});
    response update_res{check()};
    if (auto [update_ec] = co_await conn.exec(update_req, update_res, &diag); update_ec)
    {
        print_err("Update result: ", update_ec, diag);
        co_return;
    }
    std::cout << "Updated successfully\n";

    // Select
    request select_req;
    select_req.add_query("select count(*) as amount from persons", {});
    std::vector<row_count> select_vec;
    response select_res{into(select_vec)};
    if (auto [select_ec] = co_await conn.exec(select_req, select_res, &diag); select_ec)
    {
        print_err("Select result: ", select_ec, diag);
        co_return;
    }
    else if (select_vec.empty())
    {
        std::cerr << "Select count(*) resulted in empty result. This should never happen." << std::endl;
        co_return;
    }
    std::cout << "Selected: " << select_vec[0].amount << " successfully\n";

    // Delete
    request delete_req;
    delete_req.add_query("delete from persons", {});
    response delete_res{check()};
    if (auto [delete_ec] = co_await conn.exec(delete_req, delete_res, &diag); delete_ec)
    {
        print_err("Delete result: ", delete_ec, diag);
        co_return;
    }
    std::cout << "Deleted successfully\n";

    // Drop
    request drop_req;
    drop_req.add_query("drop table persons", {});
    response drop_res{check()};
    if (auto [drop_ec] = co_await conn.exec(drop_req, drop_res, &diag); drop_ec)
    {
        print_err("Drop result: ", drop_ec, diag);
        co_return;
    }
    std::cout << "Dropped successfully\n";

    // Timing Finish...
    auto finish = clock::now();
    auto d = std::chrono::duration_cast<ms>(finish - start);
    std::cout << "Timing result: " << d.count() << " [ms]\n";

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
        },
        [](const std::exception_ptr& exc) {
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
