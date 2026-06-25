//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

/*
 * Data Manipulation Language (DML) asio example
 */
#include <boost/asio/as_tuple.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/describe/class.hpp>

#include <exception>
#include <iostream>
#include <vector>
#include <chrono>
#include <cstdint>

#include "nativepg/connection.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/request.hpp"
#include "nativepg/response.hpp"

namespace asio = boost::asio;
using namespace nativepg;

struct row_count
{
    std::int64_t amount;
};
BOOST_DESCRIBE_STRUCT(row_count, (), (amount))

static void print_err(const char* prefix, const extended_error& err)
{
    std::cerr << prefix << err.code.what() << ": " << err.diag.message() << '\n';
}

static asio::awaitable<void> co_main()
{
    // Timing Start...
    using clock = std::chrono::steady_clock;
    using ms = std::chrono::milliseconds;
    auto start = clock::now();


    // Create a connection
    connection conn{co_await asio::this_coro::executor};

    // Connect
    if (auto [err] = co_await conn.async_connect(
        {.hostname = "localhost", .username = "postgres", .password = "secret", .database = "postgres"}, asio::as_tuple); err.code)
    {
        print_err("Connect result: ", err);
        co_return;
    }
    std::cout << "Startup complete\n";

    // Create
    request create_req;
    create_req.add_query("CREATE TABLE IF NOT EXISTS persons ( id bigserial primary key , name text not null, postal_code integer)", {});
    response create_res{check()};
    if (auto [create_err] = co_await conn.async_exec(create_req, create_res, asio::as_tuple); create_err.code)
    {
        print_err("Create result: ", create_err);
        co_return;
    }
    std::cout << "Created successfully\n";

    // Insert
    request insert_req;
    insert_req.add_query("INSERT INTO persons (name, postal_code) VALUES ('Bert', 1000), ('Ernie', 2000)", {});
    response insert_res{check()};
    if (auto [insert_err] = co_await conn.async_exec(insert_req, insert_res, asio::as_tuple); insert_err.code)
    {
        print_err("Insert result: ", insert_err);
        co_return;
    }
    std::cout << "Inserted successfully\n";

    // Update
    request update_req;
    update_req.add_query("UPDATE persons SET postal_code = $1 WHERE id >= $2", {3000, 0});
    response update_res{check()};
    if (auto [update_err] = co_await conn.async_exec(update_req, update_res, asio::as_tuple); update_err.code)
    {
        print_err("Update result: ", update_err);
        co_return;
    }
    std::cout << "Updated successfully\n";

    // Select
    request select_req;
    select_req.add_query("select count(*) as amount from persons", {});
    std::vector<row_count> select_vec;
    response select_res{into(select_vec)};
    if (auto [select_err] = co_await conn.async_exec(select_req, select_res, asio::as_tuple); select_err.code)
    {
        print_err("Select result: ", select_err);
        co_return;
    }
    else if (select_vec.empty())
    {
        std::cerr << "Select count(*) resulted in empty result. This should never happen!" << std::endl;
        co_return;
    }
    std::cout << "Selected: " << select_vec[0].amount << " successfully\n";

    // Delete
    request delete_req;
    delete_req.add_query("delete from persons", {});
    response delete_res{check()};
    if (auto [delete_err] = co_await conn.async_exec(delete_req, delete_res, asio::as_tuple); delete_err.code)
    {
        print_err("Delete result: ", delete_err);
        co_return;
    }
    std::cout << "Deleted successfully\n";

    // Drop
    request drop_req;
    drop_req.add_query("drop table persons", {});
    response drop_res{check()};
    if (auto [drop_err] = co_await conn.async_exec(drop_req, drop_res, asio::as_tuple); drop_err.code)
    {
        print_err("Drop result: ", drop_err);
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
    asio::io_context ctx;

    asio::co_spawn(ctx, co_main(), [](const std::exception_ptr& exc) {
        if (exc)
            std::rethrow_exception(exc);
    });

    ctx.run();
}
