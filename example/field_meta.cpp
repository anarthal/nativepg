//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/asio/as_tuple.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/address_v4.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/describe/class.hpp>

#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <string_view>
#include <vector>
#include <chrono>
#include <format>

#include "nativepg/connection.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/request.hpp"
#include "nativepg/response.hpp"

namespace asio = boost::asio;
using namespace nativepg;

struct my_row {
    std::int32_t  id;
    std::string   description;   // DB column named "title"
    types::pg_timestamptz created_at;
};
BOOST_DESCRIBE_STRUCT(my_row, (), (id, description, created_at));

// Override column name
template <>
struct nativepg::detail::field_meta<&my_row::description>
{
    static constexpr std::optional<std::string_view> column_name = "title";
    static constexpr std::optional<std::int32_t> oid = std::nullopt;
    static constexpr bool nullable = false;
};

// Pin strict OID for created_at (timestamptz = 1184)
template <>
struct nativepg::detail::field_meta<&my_row::created_at>
{
    static constexpr std::optional<std::string_view> column_name = std::nullopt;
    static constexpr std::optional<std::int32_t> oid = 1184;
    static constexpr bool nullable = false;
};


// D to std::chrono::days
static asio::awaitable<void> field_meta_text_example(connection& conn)
{
    // Start timing this operation
    auto start = std::chrono::high_resolution_clock::now();

    // Compose our arrange request
    /*
    request arrange_req;
    arrange_req
        .add_query("DROP TABLE IF EXISTS my_row", {})
        .add_query("CREATE TABLE IF NOT EXISTS my_row ("
                        "id int PRIMARY KEY,"
                        "title text,"
                        "created_at timestamptz DEFAULT now())", {})
        .add_query("INSERT INTO my_row (id, title, created_at) VALUES (1,'test')", {});

    std::vector<my_row> arrange_vec;
    response arrange_res{into(arrange_vec)};

    auto [arrange_err] = co_await conn.async_exec(arrange_req, arrange_res, asio::as_tuple);
    if (arrange_err.extended_error::code != boost::system::errc::success)
    {
        std::cerr << "ARRANGING test has error: " << arrange_err.code.what() << ": " << arrange_err.diag.message() << std::endl;
    }
    */

    request req;
    req
        .add_query("SELECT * FROM my_row;", {});

    // Structures to parse the response into
    std::vector<my_row> select_vec;
    response res{into(select_vec)};

    auto [err] = co_await conn.async_exec(req, res, asio::as_tuple);

    // Finish timing this method
    auto finish = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(finish - start);

    // Print results
    if (err.extended_error::code != boost::system::errc::success)
        std::cerr << "SELECT TEXT operation results in Error: " << err.code.what() << ": " << err.diag.message() << " (in " << duration << ")" << std::endl;
    else
        std::cout << "SELECT TEXT result: \n| " << select_vec[0].id << " | " << select_vec[0].description << " | " << select_vec[0].created_at << " | " << " (in " << duration << ")" << std::endl;
}


static asio::awaitable<void> co_main()
{
    // Create a connection
    connection conn{co_await asio::this_coro::executor};

    // Connect
    co_await conn.async_connect(
        {.hostname = "localhost", .username = "postgres", .password = "secret", .database = "postgres"}
    );
    std::cout << "Startup complete\n";

    co_await field_meta_text_example(conn);


    std::cout << "Done\n";
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