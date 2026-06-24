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
    std::string_view title;
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
    boost::multiprecision::cpp_dec_float_100 n;
    std::string_view t;
    std::string_view v;
};
BOOST_DESCRIBE_STRUCT(test_row, (), (title, b, ba, i2, i4_a, i4_b, i8_a, i8_b, i8_c, f4, f8_a, f8_b, n, t, v))

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
SELECT  'Test values' as title,
        true as b,
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
        E'\xE2\x82\xAC'::varchar as v
UNION ALL
SELECT
    'Minimum values' as title,
    false as b, -- Minimum boolean value
    E'\\x'::bytea as ba, -- Empty byte string is the minimum bytea value
    -32767::int2 as i2,
    -32767::int2 as i4_a,
    -2147483647::int4 as i4_b,
    -32767::int2 as i8_a,
    -2147483647::int4 as i8_b,
    -9223372036854775807::int8 as i8_c,
    '-Infinity'::float4 as f4,
    '-Infinity'::float8 as f8_a,
    '-Infinity'::float8 as f8_b,
    '-99999999999999999999999999999999999999999999999999.99999999999999999999999999999999999999999999999999'::numeric as n, -- Supported in PostgreSQL 14+
    ''::text as t, -- Empty string is lexicographically the smallest text
    ''::varchar as v -- Empty string is lexicographically the smallest varchar
UNION ALL
SELECT
    'Maximum values' as title,
    true as b, -- Maximum boolean value
    E'\\xFF'::bytea as ba, -- Single highest-byte representation (or repeat \xFF for larger sequences)
    32767::int2 as i2,
    32767::int2 as i4_a,
    2147483647::int4 as i4_b,
    32767::int2 as i8_a,
    2147483647::int4 as i8_b,
    9223372036854775807::int8 as i8_c,
    'Infinity'::float4 as f4,
    'Infinity'::float8 as f8_a,
    'Infinity'::float8 as f8_b,
    '99999999999999999999999999999999999999999999999999.99999999999999999999999999999999999999999999999999'::numeric as n,
    repeat(chr(1114111), 1)::text as t, -- Highest valid Unicode character (U+10FFFF)
    repeat(chr(1114111), 1)::varchar as v -- Highest valid Unicode character (U+10FFFF)
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
        std::cout << "BASE TEXT   select result: (in " << duration << ")" << std::endl;
        for (const auto& row : select_vec)
        {
            std::cout << " | " << row.title
                 << " | " << row.b
                 << " | " << row.ba
                 << " | " << row.i2
                 << " | " << row.i4_a
                 << " | " << row.i4_b
                 << " | " << row.i8_a
                 << " | " << row.i8_b
                 << " | " << row.i8_c
                 << " | " << row.f4
                 << " | " << row.f8_a
                 << " | " << row.f8_b
                 << " | " << row.n.str(100)
                 << " | " << row.t
                 << " | " << row.v
            << std::endl;
        }
        std::cout << std::endl;
    }
}

template <typename T>
static asio::awaitable<void> execute_and_print_binary_response(
    connection& conn,
    statement<T>& stmnt,
    std::initializer_list<parameter_ref> params)
{
    // Use the prepared statement
    request req{false};
    req.add_execute(stmnt.name, params, request::param_format::text, protocol::format_code::binary, 1);
    req.add_sync();

    // Structures to parse the response into
    std::vector<test_row> select_vec;
    response res{into(select_vec)};

    auto [err] = co_await conn.async_exec(req, res, asio::as_tuple);

    // Print results
    if (err.extended_error::code != boost::system::errc::success)
        std::cerr << "BASE BINARY operation results in Error: " << err.code.what() << ": " << err.diag.message() <<  std::endl;
    else
    {
        std::cout << std::boolalpha;
        std::cout << "BASE BINARY select result: | " << select_vec[0].title
                  << " | " << select_vec[0].b
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
                  << " | " << select_vec[0].n.str(100)
                  << " | " << select_vec[0].t
                  << " | " << select_vec[0].v << std::endl;
    }
}

static asio::awaitable<void> base_binary_example(connection& conn)
{
    // Start timing this operation
    auto start = std::chrono::high_resolution_clock::now();
    diagnostics diag;

    statement<std::string_view> select_stmt{"base_bintest"};

    // Compose our request
    request req{false};  // Turns off autosync for better efficiency
    req.add_prepare(R"sql(
        SELECT  $1 as title,
                 $2::text::bool as b,
                 $3::bytea as ba,
                 $4::text::int2 as i2,
                 $5::text::int4 as i4_a,
                 $6::text::int4 as i4_b,
                 $7::text::int8 as i8_a,
                 $8::text::int8 as i8_b,
                 $9::text::int8 as i8_c,
                 $10::text::float4 as f4,
                 $11::text::float8 as f8_a,
                 $12::text::float8 as f8_b,
                 $13::text::numeric as n,
                 $14::text as t,
                 $15::varchar as v
    )sql", select_stmt);
    req.add_sync();

    // Actually prepare the statements
    response res{check_parse()};
    auto [err] = co_await conn.async_exec(req, res,asio::as_tuple);
    if (err.extended_error::code != boost::system::errc::success)
    {
        print_err("Error preparing", err.code, diag);
        co_return;
    }

    co_await execute_and_print_binary_response(conn, select_stmt, { "Test values", "true", "\x21\x06\x77", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11.21061977", "twelve", "\xE2\x82\xAC"});
    co_await execute_and_print_binary_response(conn, select_stmt, { "Minimum values", "false", "", "-32767", "-32767", "-2147483647", "-32767", "-2147483647", "-9223372036854775807", "-Infinity", "-Infinity", "-Infinity", "-99999999999999999999999999999999999999999999999999.99999999999999999999999999999999999999999999999999", "", ""});
    co_await execute_and_print_binary_response(conn, select_stmt, { "Maximum values", "true", "\x0F", "32767", "32767", "2147483647", "32767", "2147483647", "9223372036854775807", "Infinity", "Infinity", "Infinity", "99999999999999999999999999999999999999999999999999.99999999999999999999999999999999999999999999999999", "n/a", "n/a"});

    // Finish timing this method
    auto finish = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(finish - start);

    std::cout << " (in " << duration << ")" << std::endl;

    req = request{false};  // TODO: replace by clear when we have it
    req.add_close_statement(select_stmt.name);
    req.add_sync();

    response res_close{check_close()};
    auto [err_cleanup] = co_await conn.async_exec(req, res_close, asio::as_tuple);
    if (err_cleanup.extended_error::code != boost::system::errc::success)
    {
        print_err("Error closing", err_cleanup.code, diag);
        co_return;
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
