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
#include <boost/asio/ip/address_v4.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/describe/class.hpp>

#include <exception>
#include <iostream>
#include <string_view>
#include <vector>
#include <chrono>

#include "nativepg/connection.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/request.hpp"
#include "nativepg/response.hpp"

namespace asio = boost::asio;
using namespace nativepg;

struct count
{
    std::int64_t amount;
};
BOOST_DESCRIBE_STRUCT(count, (), (amount))

static void print_err(const char* prefix, const extended_error& err)
{
    std::cout << prefix << err.code.what() << ": " << err.diag.message() << '\n';
}

static asio::awaitable<void> co_main()
{
    // Timing Start...
    typedef std::chrono::high_resolution_clock Time;
    typedef std::chrono::milliseconds ms;
    typedef std::chrono::duration<float> fsec;
    auto start = Time::now();


    // Create a connection
    connection conn{co_await asio::this_coro::executor};

    // Connect
    co_await conn.async_connect(
        {.hostname = "localhost", .username = "postgres", .password = "secret", .database = "postgres"}
    );
    std::cout << "Startup complete\n";

    // Create
    request create_req;
    create_req.add_query("CREATE TABLE IF NOT EXISTS ciusdd ( id bigserial primary key , name text not null, postal_code integer)", {});
    response create_res{check()};
    auto [create_err] = co_await conn.async_exec(create_req, create_res, asio::as_tuple);
    if (create_err.code)
        print_err("Create result: ", create_err);
    else
        std::cout << "Created successfully\n";

    // Insert
    request insert_req;
    insert_req.add_query("INSERT INTO ciusdd (name, postal_code) VALUES ('Bert', 1000), ('Ernie', 2000)", {});
    // insert, update, and delete statements can return the number of affected rows. How to catch them?
    // In other languages dml statements have there own execute(_scalar) method.
    //e.g. insert_req.add_scalar("INSERT INTO ciusdd (name, postal_code) VALUES ('Bert', 1000), ('Ernie', 2000)"); // So default empty params
    //  or insert_req.add_dml("INSERT INTO ciusdd (name, postal_code) VALUES ('Bert', 1000), ('Ernie', 2000)"); // So default empty params
    //  or insert_res.affected_rows(); ???
    response insert_res{check()};
    auto [insert_err] = co_await conn.async_exec(insert_req, insert_res, asio::as_tuple);
    if (insert_err.code)
        print_err("Insert result: ", insert_err);
    else
        std::cout << "Inserted successfully\n";

    // Update
    request update_req;
    update_req.add_query("UPDATE ciusdd SET postal_code = 3000", {});
    response update_res{check()};
    auto [update_err] = co_await conn.async_exec(update_req, update_res, asio::as_tuple);
    if (update_err.code)
        print_err("Update result: ", update_err);
    else
        std::cout << "Update successfully\n";

    // Select
    request select_req;
    select_req.add_query("select count(*) as amount from ciusdd", {});
    std::vector<count> select_vec;
    response select_res{into(select_vec)};
    auto [select_err] = co_await conn.async_exec(select_req, select_res, asio::as_tuple);
    if (select_err.code)
        print_err("Select result: ", select_err);
    else
        std::cout << "Selected: " << select_vec[0].amount << " successfully\n";

    // Delete
    request delete_req;
    delete_req.add_query("delete from ciusdd", {});
    response delete_res{check()};
    auto [delete_err] = co_await conn.async_exec(delete_req, delete_res, asio::as_tuple);
    if (delete_err.code)
        print_err("Delete result: ", delete_err);
    else
        std::cout << "Deleted successfully\n";

    // Drop
    request drop_req;
    drop_req.add_query("drop table ciusdd;", {});
    response drop_res{check()};
    auto [drop_err] = co_await conn.async_exec(drop_req, drop_res, asio::as_tuple);
    if (drop_err.code)
        print_err("Drop result: ", drop_err);
    else
        std::cout << "Dropped successfully\n";

    std::cout << "Done\n";

    // Timing Finish...
    auto finish = Time::now();
    fsec fs = finish - start;
    auto d = std::chrono::duration_cast<ms>(fs);
    std::cout << d.count() << " ms (" << fs.count() << "s )\n";
}

int main()
{
    asio::io_context ctx;

    asio::co_spawn(ctx, co_main(), [](std::exception_ptr exc) {
        if (exc)
            std::rethrow_exception(exc);
    });

    ctx.run();
}
