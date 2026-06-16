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
#include <boost/json.hpp>

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

struct json_row
{
    types::pg_json j;
};
BOOST_DESCRIBE_STRUCT(json_row, (), (j))

struct jsonb_row
{
    types::pg_jsonb jb;
};
BOOST_DESCRIBE_STRUCT(jsonb_row, (), (jb))


// JSON to types::pg_json (TEXT)
static asio::awaitable<void> json_text_example(connection& conn)
{
    // Start timing this operation
    auto start = std::chrono::high_resolution_clock::now();

    // Compose our request
    request req;
    req.add_query("SELECT '{ \"name\": \"John\", \"age\": 30, \"address\": { \"street\": \"Main St\", \"city\": \"New York\" }}'::json as j", {});

    // Structures to parse the response into
    std::vector<json_row> select_vec;
    response res{into(select_vec)};

    auto [err] = co_await conn.async_exec(req, res, asio::as_tuple);

    // Finish timing this method
    auto finish = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(finish - start);

    // Print results
    if (err.extended_error::code != boost::system::errc::success)
        std::cerr << "JSON TEXT operation results in Error: " << err.code.what() << ": " << err.diag.message() << " (in " << duration << ")" << std::endl;
    else
        std::cout << "JSON TEXT select result: " << boost::json::serialize(select_vec[0].j.get()) << " (in " << duration << ")" << std::endl;
}

// JSON to types::pg_json (BINARY)
static asio::awaitable<void> json_binary_example(connection& conn)
{
    // Start timing this operation
    auto start = std::chrono::high_resolution_clock::now();

    // Compose our request
    request req;
    req.add_prepare("SELECT $1::text::json as j", {"json_bintest"} )
        .add_execute("json_bintest", {"{ \"name\": \"John\", \"age\": 30, \"address\": { \"street\": \"Main St\", \"city\": \"New York\" }}"}, request::param_format::text, protocol::format_code::binary, 1);

    // Structures to parse the response into
    std::vector<json_row> select_vec;
    response res{into(select_vec)};

    auto [err] = co_await conn.async_exec(req, res, asio::as_tuple);

    // Finish timing this method
    auto finish = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(finish - start);

    // Print results
    if (err.extended_error::code != boost::system::errc::success)
        std::cerr << "JSON BINARY Error: " << err.code.what() << ": " << err.diag.message() << " (in " << duration << ")" << std::endl;
    else
        std::cout << "JSON BINARY select result: " << boost::json::serialize(select_vec[0].j.get()) << " (in " << duration << ")" << std::endl;
}


// JSONB to types::pg_jsonb (TEXT)
static asio::awaitable<void> jsonb_text_example(connection& conn)
{
    // Start timing this operation
    auto start = std::chrono::high_resolution_clock::now();

    // Compose our request
    request req;
    req.add_query("SELECT '{ \"name\": \"John\", \"age\": 30, \"address\": { \"street\": \"Main St\", \"city\": \"New York\" }}'::jsonb as jb", {});

    // Structures to parse the response into
    std::vector<jsonb_row> select_vec;
    response res{into(select_vec)};

    auto [err] = co_await conn.async_exec(req, res, asio::as_tuple);

    // Finish timing this method
    auto finish = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(finish - start);

    // Print results
    if (err.extended_error::code != boost::system::errc::success)
        std::cerr << "JSONB TEXT operation results in Error: " << err.code.what() << ": " << err.diag.message() << " (in " << duration << ")" << std::endl;
    else
        std::cout << "JSONB TEXT select result: " << boost::json::serialize(select_vec[0].jb.get()) << " (in " << duration << ")" << std::endl;
}

// JSONB to types::pg_jsonb (BINARY)
static asio::awaitable<void> jsonb_binary_example(connection& conn)
{
    // Start timing this operation
    auto start = std::chrono::high_resolution_clock::now();

    // Compose our request
    request req;
    req.add_prepare("SELECT $1::jsonb as jb", {"jsonb_bintest"} )
        .add_execute("jsonb_bintest", {"{ \"name\": \"John\", \"age\": 30, \"address\": { \"street\": \"Main St\", \"city\": \"New York\" }}"}, request::param_format::text, protocol::format_code::binary, 1);

    // Structures to parse the response into
    std::vector<jsonb_row> select_vec;
    response res{into(select_vec)};

    auto [err] = co_await conn.async_exec(req, res, asio::as_tuple);

    // Finish timing this method
    auto finish = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(finish - start);

    // Print results
    if (err.extended_error::code != boost::system::errc::success)
        std::cerr << "JSONB BINARY Error: " << err.code.what() << ": " << err.diag.message() << " (in " << duration << ")" << std::endl;
    else
        std::cout << "JSONB BINARY select result: " << boost::json::serialize(select_vec[0].jb.get()) << " (in " << duration << ")" << std::endl;
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

    co_await json_text_example(conn);
    co_await json_binary_example(conn);

    co_await jsonb_text_example(conn);
    co_await jsonb_binary_example(conn);

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