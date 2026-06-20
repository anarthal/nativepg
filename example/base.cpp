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
#include "nativepg/types/base.hpp"

namespace asio = boost::asio;
using namespace nativepg;

struct test_row
{
    bool b;
    std::vector<std::byte> ba;
    std::int16_t i2;
    std::int32_t i4_a;
    std::int32_t i4_b;
    std::int64_t i8_a;
    std::int64_t i8_b;
    std::int64_t i8_c;
    float f4;
    double f8_a;
    double f8_b;
    boost::multiprecision::cpp_dec_float_50 n;
    std::string_view t;
    std::string_view v;
};
BOOST_DESCRIBE_STRUCT(test_row, (), (b, ba, i2, i4_a, i4_b, i8_a, i8_b, i8_c, f4, f8_a, f8_b, n, t, v))

static void print_err(const char* prefix, std::error_code err, const diagnostics& diag)
{
    std::cerr << prefix << ": " << err << ": " << err.message();
    if (!diag.message().empty())
        std::cerr << ": " << diag.message();
    std::cerr << '\n';
}

static asio::awaitable<void> base_text_example(connection& conn)
{
    // Start timing this operation
    auto start = std::chrono::high_resolution_clock::now();

    // Compose our request
    request req;
    req.add_query(R"sql(
        SELECT  true as b,
                 E'\x21\x06\x77'::bytea as ba,
                 2::int2 as i2,
                 3::int2 as i4_a,
                 4::int4 as i4_b,
                 5::int2 as i8_a,
                 6::int4 as i8_b,
                 7::int8 as i8_c,
                 8.0::float4 as f4,
                 9.0::float8 as f8_a,
                 10.0::float8 as f8_b,
                 11.21061977::numeric as n,
                 'twelve'::text as t,
                 'thirteen'::varchar as v
    )sql", {});

    // Structures to parse the response into
    std::vector<test_row> select_vec;
    response res{into(select_vec)};
    diagnostics diag;

    auto [err] = co_await conn.async_exec(req, res, asio::as_tuple);

    // Finish timing this method
    auto finish = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(finish - start);

    // Print results
    if (err.extended_error::code != boost::system::errc::success)
        std::cerr << "BASE TEXT operation results in Error: " << err.code.what() << ": " << err.diag.message() << " (in " << duration << ")" << std::endl;
    else
    {
        std::cout << std::boolalpha;
        std::cout << "BASE TEXT   select result: | " << select_vec[0].b
                  << " | " << select_vec[0].ba
                  << " | " << select_vec[0].i2
                  << " | " << select_vec[0].i4_a
                  << " | " << select_vec[0].i4_b
                  << " | " << select_vec[0].i8_a
                  << " | " << select_vec[0].i8_b
                  << " | " << select_vec[0].i8_c
                  << " | " << select_vec[0].f4
                  << " | " << select_vec[0].f8_a
                  << " | " << select_vec[0].f8_b
                  << " | " << select_vec[0].n
                  << " | " << select_vec[0].t
                  << " | " << select_vec[0].v
                  << " | (in " << duration << ")" << std::endl;
    }
}

static asio::awaitable<void> base_binary_example(connection& conn)
{
    // Start timing this operation
    auto start = std::chrono::high_resolution_clock::now();

    // Compose our request
    request req;
    req.add_prepare(R"sql(
        SELECT  $1::text::bool as b,
                 $2::bytea as ba,
                 $3::text::int2 as i2,
                 $4::text::int4 as i4_a,
                 $5::text::int4 as i4_b,
                 $6::text::int8 as i8_a,
                 $7::text::int8 as i8_b,
                 $8::text::int8 as i8_c,
                 $9::text::float4 as f4,
                 $10::text::float8 as f8_a,
                 $11::text::float8 as f8_b,
                 $12::text::numeric as n,
                 $13::text as t,
                 $14::varchar as v
    )sql", {"base_bintest"})
        .add_execute("base_bintest", {
            "true",
            "\x21\x06\x77",
            "2",
            "3",
            "4",
            "5",
            "6",
            "7",
            "8",
            "9",
            "10",
            "11.21061977",
            "twelve",
            "thirteen"},
            request::param_format::text, protocol::format_code::binary, 1);

    // Structures to parse the response into
    std::vector<test_row> select_vec;
    response res{into(select_vec)};

    auto [err] = co_await conn.async_exec(req, res, asio::as_tuple);

    // Finish timing this method
    auto finish = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(finish - start);

    // Print results
    if (err.extended_error::code != boost::system::errc::success)
        std::cerr << "BASE BINARY operation results in Error: " << err.code.what() << ": " << err.diag.message() << " (in " << duration << ")" << std::endl;
    else
    {
        std::cout << std::boolalpha;
        std::cout << "BASE BINARY select result: | " << select_vec[0].b
                  << " | " << select_vec[0].ba
                  << " | " << select_vec[0].i2
                  << " | " << select_vec[0].i4_a
                  << " | " << select_vec[0].i4_b
                  << " | " << select_vec[0].i8_a
                  << " | " << select_vec[0].i8_b
                  << " | " << select_vec[0].i8_c
                  << " | " << select_vec[0].f4
                  << " | " << select_vec[0].f8_a
                  << " | " << select_vec[0].f8_b
                  << " | " << select_vec[0].n
                  << " | " << select_vec[0].t
                  << " | " << select_vec[0].v
                  << " | (in " << duration << ")" << std::endl;
    }
}

static asio::awaitable<void> co_main()
{
    diagnostics diag;

    // Create a connection
    connection conn{co_await asio::this_coro::executor};

    // Connect
    co_await conn.async_connect(
        {.hostname = "localhost", .username = "postgres", .password = "secret", .database = "postgres"}
    );
    std::cout << "Startup complete\n";

    co_await base_text_example(conn);
    co_await base_binary_example(conn);

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