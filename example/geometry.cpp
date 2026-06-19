//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#define NATIVEPG_GEOMETRY

#include <boost/asio/as_tuple.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/address_v4.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/describe/class.hpp>

#include <exception>
#include <iomanip>
#include <iostream>
#include <string_view>
#include <vector>
#include <chrono>

#include "nativepg/connection.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/request.hpp"
#include "nativepg/response.hpp"
#include "nativepg/types/geometry.hpp"

namespace asio = boost::asio;
using namespace nativepg;

struct geom_row
{
    types::pg_geometry<> g;
};
BOOST_DESCRIBE_STRUCT(geom_row, (), (g))


// POINT to types::pg_geometry (TEXT)
static asio::awaitable<void> point_text_example(connection& conn)
{
    // Start timing this operation
    auto start = std::chrono::high_resolution_clock::now();

    // Compose our request
    request req;
    req.add_query("SELECT 'POINT(1.1977 2.1426)'::geometry as g", {});

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

// POINT to types::pg_geometry (BINARY)
static asio::awaitable<void> point_binary_example(connection& conn)
{
    // Start timing this operation
    auto start = std::chrono::high_resolution_clock::now();

    // Compose our request
    request req;
    req.add_prepare("SELECT $1::text::geometry as g", {"point_bintest"} )
        .add_execute("point_bintest", {"POINT(1.1977 2.1426)"}, request::param_format::text, protocol::format_code::binary, 1);

    // Structures to parse the response into
    std::vector<geom_row> select_vec;
    response res{into(select_vec)};

    auto [err] = co_await conn.async_exec(req, res, asio::as_tuple);

    // Finish timing this method
    auto finish = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(finish - start);

    // Print results
    if (err.extended_error::code != boost::system::errc::success)
        std::cerr << "POINT BINARY Error: " << err.code.what() << ": " << err.diag.message() << " (in " << duration << ")" << std::endl;
    else
        std::cout << "POINT BINARY select result: " << select_vec[0].g << " (in " << duration << ")" << std::endl;
}


// LINESTRING to types::pg_geometry (TEXT)
static asio::awaitable<void> line_text_example(connection& conn)
{
    // Start timing this operation
    auto start = std::chrono::high_resolution_clock::now();

    // Compose our request
    request req;
    req.add_query("SELECT 'LINESTRING(1.1977 2.1426, 2.5000 4.7713)'::geometry as g", {});

    // Structures to parse the response into
    std::vector<geom_row> select_vec;
    response res{into(select_vec)};

    auto [err] = co_await conn.async_exec(req, res, asio::as_tuple);

    // Finish timing this method
    auto finish = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(finish - start);

    // Print results
    if (err.extended_error::code != boost::system::errc::success)
        std::cerr << "LINESTRING TEXT operation results in Error: " << err.code.what() << ": " << err.diag.message() << " (in " << duration << ")" << std::endl;
    else
        std::cout << "LINESTRING TEXT select result: " << select_vec[0].g << " (in " << duration << ")" << std::endl;
}

// LINESTRING to types::pg_geometry (BINARY)
static asio::awaitable<void> line_binary_example(connection& conn)
{
    // Start timing this operation
    auto start = std::chrono::high_resolution_clock::now();

    // Compose our request
    request req;
    req.add_prepare("SELECT $1::text::geometry as g", {"line_bintest"} )
        .add_execute("line_bintest", {"LINESTRING(1.1977 2.1426, 2.5000 4.7713)"}, request::param_format::text, protocol::format_code::binary, 1);

    // Structures to parse the response into
    std::vector<geom_row> select_vec;
    response res{into(select_vec)};

    auto [err] = co_await conn.async_exec(req, res, asio::as_tuple);

    // Finish timing this method
    auto finish = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(finish - start);

    // Print results
    if (err.extended_error::code != boost::system::errc::success)
        std::cerr << "LINESTRING BINARY Error: " << err.code.what() << ": " << err.diag.message() << " (in " << duration << ")" << std::endl;
    else
        std::cout << "LINESTRING BINARY select result: " << select_vec[0].g << " (in " << duration << ")" << std::endl;
}

// POLYGON to types::pg_geometry (TEXT)
static asio::awaitable<void> polygon_text_example(connection& conn)
{
    // Start timing this operation
    auto start = std::chrono::high_resolution_clock::now();

    // Compose our request
    request req;
    req.add_query("SELECT 'POLYGON((1.1977 2.1426, 2.5000 4.7713, 1.977 2.5000, 1.1977 2.1426))'::geometry as g", {});

    // Structures to parse the response into
    std::vector<geom_row> select_vec;
    response res{into(select_vec)};

    auto [err] = co_await conn.async_exec(req, res, asio::as_tuple);

    // Finish timing this method
    auto finish = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(finish - start);

    // Print results
    if (err.extended_error::code != boost::system::errc::success)
        std::cerr << "POLYGON TEXT operation results in Error: " << err.code.what() << ": " << err.diag.message() << " (in " << duration << ")" << std::endl;
    else
        std::cout << "POLYGON TEXT select result: " << select_vec[0].g << " (in " << duration << ")" << std::endl;
}

// POLYGON to types::pg_geometry (BINARY)
static asio::awaitable<void> polygon_binary_example(connection& conn)
{
    // Start timing this operation
    auto start = std::chrono::high_resolution_clock::now();

    // Compose our request
    request req;
    req.add_prepare("SELECT $1::text::geometry as g", {"poly_bintest"} )
        .add_execute("poly_bintest", {"POLYGON((1.1977 2.1426, 2.5000 4.7713, 1.977 2.5000, 1.1977 2.1426))"}, request::param_format::text, protocol::format_code::binary, 1);

    // Structures to parse the response into
    std::vector<geom_row> select_vec;
    response res{into(select_vec)};

    auto [err] = co_await conn.async_exec(req, res, asio::as_tuple);

    // Finish timing this method
    auto finish = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(finish - start);

    // Print results
    if (err.extended_error::code != boost::system::errc::success)
        std::cerr << "POLYGON BINARY Error: " << err.code.what() << ": " << err.diag.message() << " (in " << duration << ")" << std::endl;
    else
        std::cout << "POLYGON BINARY select result: " << select_vec[0].g << " (in " << duration << ")" << std::endl;
}


// MULTIPOINT to types::pg_geometry (TEXT)
static asio::awaitable<void> multipoint_text_example(connection& conn)
{
    // Start timing this operation
    auto start = std::chrono::high_resolution_clock::now();

    // Compose our request
    request req;
    req.add_query("SELECT 'MULTIPOINT((100 100), (200 200))'::geometry as g", {});

    // Structures to parse the response into
    std::vector<geom_row> select_vec;
    response res{into(select_vec)};

    auto [err] = co_await conn.async_exec(req, res, asio::as_tuple);

    // Finish timing this method
    auto finish = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(finish - start);

    // Print results
    if (err.extended_error::code != boost::system::errc::success)
        std::cerr << "MULTIPOINT TEXT operation results in Error: " << err.code.what() << ": " << err.diag.message() << " (in " << duration << ")" << std::endl;
    else
        std::cout << "MULTIPOINT TEXT select result: " << select_vec[0].g << " (in " << duration << ")" << std::endl;
}

// MULTIPOINT to types::pg_geometry (BINARY)
static asio::awaitable<void> multipoint_binary_example(connection& conn)
{
    // Start timing this operation
    auto start = std::chrono::high_resolution_clock::now();

    // Compose our request
    request req;
    req.add_prepare("SELECT $1::text::geometry as g", {"multipoint_bintest"} )
        .add_execute("multipoint_bintest", {"MULTIPOINT((100 100), (200 200))"}, request::param_format::text, protocol::format_code::binary, 1);

    // Structures to parse the response into
    std::vector<geom_row> select_vec;
    response res{into(select_vec)};

    auto [err] = co_await conn.async_exec(req, res, asio::as_tuple);

    // Finish timing this method
    auto finish = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(finish - start);

    // Print results
    if (err.extended_error::code != boost::system::errc::success)
        std::cerr << "MULTIPOINT BINARY Error: " << err.code.what() << ": " << err.diag.message() << " (in " << duration << ")" << std::endl;
    else
        std::cout << "MULTIPOINT BINARY select result: " << select_vec[0].g << " (in " << duration << ")" << std::endl;
}


// MULTILINESTRING to types::pg_geometry (TEXT)
static asio::awaitable<void> multiline_text_example(connection& conn)
{
    // Start timing this operation
    auto start = std::chrono::high_resolution_clock::now();

    // Compose our request
    request req;
    req.add_query("SELECT 'MULTILINESTRING((1.1977 2.1426, 2.5000 4.7713), (4.1977 2.1426, 5.5000 6.7713))'::geometry as g", {});

    // Structures to parse the response into
    std::vector<geom_row> select_vec;
    response res{into(select_vec)};

    auto [err] = co_await conn.async_exec(req, res, asio::as_tuple);

    // Finish timing this method
    auto finish = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(finish - start);

    // Print results
    if (err.extended_error::code != boost::system::errc::success)
        std::cerr << "MULTILINESTRING TEXT operation results in Error: " << err.code.what() << ": " << err.diag.message() << " (in " << duration << ")" << std::endl;
    else
        std::cout << "MULTILINESTRING TEXT select result: " << select_vec[0].g << " (in " << duration << ")" << std::endl;
}

// MULTILINESTRING to types::pg_geometry (BINARY)
static asio::awaitable<void> multiline_binary_example(connection& conn)
{
    // Start timing this operation
    auto start = std::chrono::high_resolution_clock::now();

    // Compose our request
    request req;
    req.add_prepare("SELECT $1::text::geometry as g", {"multiline_bintest"} )
        .add_execute("multiline_bintest", {"MULTILINESTRING((1.1977 2.1426, 2.5000 4.7713), (4.1977 2.1426, 5.5000 6.7713))"}, request::param_format::text, protocol::format_code::binary, 1);

    // Structures to parse the response into
    std::vector<geom_row> select_vec;
    response res{into(select_vec)};

    auto [err] = co_await conn.async_exec(req, res, asio::as_tuple);

    // Finish timing this method
    auto finish = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(finish - start);

    // Print results
    if (err.extended_error::code != boost::system::errc::success)
        std::cerr << "MULTILINESTRING BINARY Error: " << err.code.what() << ": " << err.diag.message() << " (in " << duration << ")" << std::endl;
    else
        std::cout << "MULTILINESTRING BINARY select result: " << select_vec[0].g << " (in " << duration << ")" << std::endl;
}

// MULTIPOLYGON to types::pg_geometry (TEXT)
static asio::awaitable<void> multipolygon_text_example(connection& conn)
{
    // Start timing this operation
    auto start = std::chrono::high_resolution_clock::now();

    // Compose our request
    request req;
    req.add_query("SELECT 'MULTIPOLYGON (((1.1977 2.1426, 2.5 4.7713, 1.977 2.5, 1.1977 2.1426), (10.1977 20.1426, 20.5 40.7713, 10.977 20.5, 10.1977 20.1426)))'::geometry as g", {});

    // Structures to parse the response into
    std::vector<geom_row> select_vec;
    response res{into(select_vec)};

    auto [err] = co_await conn.async_exec(req, res, asio::as_tuple);

    // Finish timing this method
    auto finish = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(finish - start);

    // Print results
    if (err.extended_error::code != boost::system::errc::success)
        std::cerr << "MULTIPOLYGON TEXT operation results in Error: " << err.code.what() << ": " << err.diag.message() << " (in " << duration << ")" << std::endl;
    else
        std::cout << "MULTIPOLYGON TEXT select result: " << select_vec[0].g << " (in " << duration << ")" << std::endl;
}

// MULTIPOLYGON to types::pg_geometry (BINARY)
static asio::awaitable<void> multipolygon_binary_example(connection& conn)
{
    // Start timing this operation
    auto start = std::chrono::high_resolution_clock::now();

    // Compose our request
    request req;
    req.add_prepare("SELECT $1::text::geometry as g", {"multipoly_bintest"} )
        .add_execute("multipoly_bintest", {"MULTIPOLYGON (((1.1977 2.1426, 2.5 4.7713, 1.977 2.5, 1.1977 2.1426), (10.1977 20.1426, 20.5 40.7713, 10.977 20.5, 10.1977 20.1426)))"}, request::param_format::text, protocol::format_code::binary, 1);

    // Structures to parse the response into
    std::vector<geom_row> select_vec;
    response res{into(select_vec)};

    auto [err] = co_await conn.async_exec(req, res, asio::as_tuple);

    // Finish timing this method
    auto finish = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(finish - start);

    // Print results
    if (err.extended_error::code != boost::system::errc::success)
        std::cerr << "MULTIPOLYGON BINARY Error: " << err.code.what() << ": " << err.diag.message() << " (in " << duration << ")" << std::endl;
    else
        std::cout << "MULTIPOLYGON BINARY select result: " << select_vec[0].g << " (in " << duration << ")" << std::endl;
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

    co_await line_text_example(conn);
    co_await line_binary_example(conn);

    co_await polygon_text_example(conn);
    co_await polygon_binary_example(conn);


    co_await multipoint_text_example(conn);
    co_await multipoint_binary_example(conn);

    co_await multiline_text_example(conn);
    co_await multiline_binary_example(conn);

    co_await multipolygon_text_example(conn);
    co_await multipolygon_binary_example(conn);

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