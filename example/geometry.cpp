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

struct geom_row
{
    types::pg_geometry g;
};
BOOST_DESCRIBE_STRUCT(geom_row, (), (g))


// POINT to types::pg_geometry
static asio::awaitable<void> point_text_example(connection& conn)
{
    // Start timing this operation
    auto start = std::chrono::high_resolution_clock::now();

    // Compose our request
    request req;
    req.add_query("SELECT 'POINT(1.1977,2.1426)'::geometry as p", {});

    // Structures to parse the response into
    std::vector<geom_row> select_vec;
    response res{into(select_vec)};

    auto [err] = co_await conn.async_exec(req, res, asio::as_tuple);

    // Finish timing this method
    auto finish = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(finish - start);

    // Print results
    if (err.extended_error::code != boost::system::errc::success)
        std::cerr << "POINT TEXT operation results in Error: " << err.code.what() << ": " << err.diag.message() << " (in " << duration << ")" << std::endl;
    else
        std::cout << "POINT TEXT select result: " << select_vec[0].g << " (in " << duration << ")" << std::endl;
}

static asio::awaitable<void> point_binary_example(connection& conn)
{
    // Start timing this operation
    auto start = std::chrono::high_resolution_clock::now();

    // Compose our request
    request req;
    req.add_prepare("SELECT $1::text::geometry as p", {"point_bintest"} )
        .add_execute("point_bintest", {"POINT(1.1977,2.1426)"}, request::param_format::text, protocol::format_code::binary, 1);

    // Structures to parse the response into
    std::vector<geom_row> select_vec;
    response res{into(select_vec)};

    auto [err] = co_await conn.async_exec(req, res, asio::as_tuple);

    // Finish timing this method
    auto finish = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(finish - start);

    // Print results
    if (err.extended_error::code != boost::system::errc::success)
        std::cerr << "DATE BINARY Error: " << err.code.what() << ": " << err.diag.message() << " (in " << duration << ")" << std::endl;
    else
        std::cout << "DATE BINARY select result: " << select_vec[0].g << " (in " << duration << ")" << std::endl;
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

    co_await point_text_example(conn);
    co_await point_binary_example(conn);

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