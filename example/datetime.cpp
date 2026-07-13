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

struct date_row
{
    //std::chrono::sys_days d;
    types::pg_date d;
};
BOOST_DESCRIBE_STRUCT(date_row, (), (d))

struct time_row
{
    //std::chrono::microseconds t;
    types::pg_time t;
};
BOOST_DESCRIBE_STRUCT(time_row, (), (t))

struct timetz_row
{
    types::pg_timetz tz;
};
BOOST_DESCRIBE_STRUCT(timetz_row, (), (tz))

struct timestamp_row
{
    types::pg_timestamp ts;
};
BOOST_DESCRIBE_STRUCT(timestamp_row, (), (ts))

struct timestamptz_row
{
    types::pg_timestamptz tsz;
};
BOOST_DESCRIBE_STRUCT(timestamptz_row, (), (tsz))

struct interval_row
{
    types::pg_interval iv;
};
BOOST_DESCRIBE_STRUCT(interval_row, (), (iv))


// DATE to std::chrono::days
static asio::awaitable<void> date_text_example(connection& conn)
{
    // Start timing this operation
    auto start = std::chrono::high_resolution_clock::now();

    // Compose our request
    request req;
    req.add_query("SELECT DATE '1977-06-21' as d", {});

    // Structures to parse the response into
    std::vector<date_row> select_vec;
    response res{into(select_vec)};

    auto [err] = co_await conn.async_exec(req, res, asio::as_tuple);

    // Finish timing this method
    auto finish = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(finish - start);

    // Print results
    if (err.extended_error::code != boost::system::errc::success)
        std::cerr << "DATE TEXT operation results in Error: " << err.code.what() << ": " << err.diag.message() << " (in " << duration << ")" << std::endl;
    else
        std::cout << "DATE TEXT select result: " << std::format("{0:%F}", select_vec[0].d) << " (in " << duration << ")" << std::endl;
}

static asio::awaitable<void> date_binary_example(connection& conn)
{
    // Start timing this operation
    auto start = std::chrono::high_resolution_clock::now();

    // Compose our request
    request req;
    req.add_prepare("SELECT $1::text::date as d", {"date_bintest"} )
        .add_execute("date_bintest", {"1977-06-21"}, request::param_format::text, protocol::format_code::binary, 1);

    // Structures to parse the response into
    std::vector<date_row> select_vec;
    response res{into(select_vec)};

    auto [err] = co_await conn.async_exec(req, res, asio::as_tuple);

    // Finish timing this method
    auto finish = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(finish - start);

    // Print results
    if (err.extended_error::code != boost::system::errc::success)
        std::cerr << "DATE BINARY Error: " << err.code.what() << ": " << err.diag.message() << " (in " << duration << ")" << std::endl;
    else
        std::cout << "DATE BINARY select result: " << std::format("{0}", select_vec[0].d) << " (in " << duration << ")" << std::endl;
}

// TIME to std::chrono::microseconds
static asio::awaitable<void> time_text_example(connection& conn)
{
    // Start timing this method
    auto start = std::chrono::high_resolution_clock::now();

    // Compose our request
    request req;
    req.add_query("SELECT TIME '12:32:06.342156' as t", {});

    // Structures to parse the response into
    std::vector<time_row> select_vec;
    response res{into(select_vec)};

    auto [err] = co_await conn.async_exec(req, res, asio::as_tuple);

    // Finish timing this method
    auto finish = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(finish - start);

    // Print results
    if (err.extended_error::code != boost::system::errc::success)
        std::cerr << "TIME TEXT operation results in Error: " << err.code.what() << ": " << err.diag.message() << " (in " << duration << ")" << std::endl;
    else
        std::cout << "TIME TEXT select result: " << std::format("{0:%T}", select_vec[0].t) << " (in " << duration << ")" << std::endl;
}

static asio::awaitable<void> time_binary_example(connection& conn)
{
    // Start timing this operation
    auto start = std::chrono::high_resolution_clock::now();

    // Compose our request
    request req;
    req.add_prepare("SELECT $1::text::time as t", {"bintest"} )
        .add_execute("bintest", {"12:34:23.43535"}, request::param_format::text, protocol::format_code::binary, 1);

    // Structures to parse the response into
    std::vector<time_row> time_vec;
    response res{into(time_vec)};

    auto [err] = co_await conn.async_exec(req, res, asio::as_tuple);

    // Finish timing this method
    auto finish = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(finish - start);

    // Print results
    if (err.extended_error::code != boost::system::errc::success)
        std::cerr << "TIME BINARY Error: " << err.code.what() << ": " << err.diag.message() << " (in " << duration << ")" << std::endl;
    else
        std::cout << "TIME BINARY select result: " << std::format("{0:%T}", time_vec[0].t) << " (in " << duration << ")" << std::endl;
}


// TIMETZ to pg_timetz
static asio::awaitable<void> timetz_text_example(connection& conn)
{
    // Start timing this operation
    auto start = std::chrono::high_resolution_clock::now();

    // Compose our request
    request req;
    req.add_query("SELECT TIMETZ '12:32:06.3421+01:00' as tz", {});

    // Structures to parse the response into
    std::vector<timetz_row> select_vec;
    response res{into(select_vec)};

    auto [err] = co_await conn.async_exec(req, res, asio::as_tuple);

    // Finish timing this method
    auto finish = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(finish - start);

    // Print results
    if (err.extended_error::code != boost::system::errc::success)
        std::cerr << "TIMETZ TEXT results in Error: " << err.code.what() << ": " << err.diag.message() << " (in " << duration << ")" << std::endl;
    else
        std::cout << "TIMETZ TEXT select result: " << std::format("{0:%T}", select_vec[0].tz.time_since_midnight) << "+"
                    << std::format("{:%T}", select_vec[0].tz.utc_offset) << " (in " << duration << ")" << std::endl;
}

static asio::awaitable<void> timetz_binary_example(connection& conn)
{
    // Start timing this operation
    auto start = std::chrono::high_resolution_clock::now();

    // Compose our request
    request req;
    req.add_prepare("SELECT $1::text::timetz as tz", {"timetz_bintest"} )
        .add_execute("timetz_bintest", {"12:34:23.43535+05:00"}, request::param_format::text, protocol::format_code::binary, 1);

    // Structures to parse the response into
    std::vector<timetz_row> select_vec;
    response res{into(select_vec)};

    auto [err] = co_await conn.async_exec(req, res, asio::as_tuple);

    // Finish timing this method
    auto finish = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(finish - start);

    // Print results
    if (err.extended_error::code != boost::system::errc::success)
        std::cerr << "TIMETZ BINARY Error: " << err.code.what() << ": " << err.diag.message() << " (in " << duration << ")" << std::endl;
    else
        std::cout << "TIMETZ BINARY select result: " << std::format("{:%T}", select_vec[0].tz.time_since_midnight)
                    << (select_vec[0].tz.utc_offset.count() > 0 ? "+"  : "") << std::format("{:%T}", select_vec[0].tz.utc_offset) << " (in " << duration << ")" << std::endl;
}

// TIMESTAMP to pg_timestamp
static asio::awaitable<void> timestamp_text_example(connection& conn)
{
    // Start timing this operation
    auto start = std::chrono::high_resolution_clock::now();

    // Compose our request
    request req;
    req.add_query("SELECT CURRENT_TIMESTAMP::timestamp as ts", {});

    // Structures to parse the response into
    std::vector<timestamp_row> select_vec;
    response res{into(select_vec)};

    auto [err] = co_await conn.async_exec(req, res, asio::as_tuple);

    // Finish timing this method
    auto finish = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(finish - start);

    // Print results
    if (err.extended_error::code != boost::system::errc::success)
        std::cerr << "TIMESTAMP TEXT results in Error: " << err.code.what() << ": " << err.diag.message() << " (in " << duration << ")" << std::endl;
    else
        std::cout << "TIMESTAMP TEXT select result: " << std::format("{0:%F} {0:%T}", select_vec[0].ts)
                    << " (in " << duration << ")" << std::endl;
}

static asio::awaitable<void> timestamp_binary_example(connection& conn)
{
    // Start timing this operation
    auto start = std::chrono::high_resolution_clock::now();

    // Compose our request
    request req;
    req.add_prepare("SELECT $1::text::timestamp as ts", {"timestamp_bintest"} )
        .add_execute("timestamp_bintest", {"2026-02-08 12:34:23.43535"}, request::param_format::text, protocol::format_code::binary, 1);

    // Structures to parse the response into
    std::vector<timestamp_row> select_vec;
    response res{into(select_vec)};

    auto [err] = co_await conn.async_exec(req, res, asio::as_tuple);

    // Finish timing this method
    auto finish = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(finish - start);

    // Print results
    if (err.extended_error::code != boost::system::errc::success)
        std::cerr << "TIMESTAMP BINARY Error: " << err.code.what() << ": " << err.diag.message() << " (in " << duration << ")" << std::endl;
    else
        std::cout << "TIMESTAMP BINARY select result: " << std::format("{0:%F} {0:%T}", select_vec[0].ts)
                    <<  " (in " << duration << ")" << std::endl;
}


// TIMESTAMPTZ to pg_timestamptz
static asio::awaitable<void> timestamptz_text_example(connection& conn)
{
    // Start timing this operation
    auto start = std::chrono::high_resolution_clock::now();

    // Compose our request
    request req;
    req.add_query("SELECT CURRENT_TIMESTAMP as tsz", {});

    // Structures to parse the response into
    std::vector<timestamptz_row> select_vec;
    response res{into(select_vec)};

    auto [err] = co_await conn.async_exec(req, res, asio::as_tuple);

    // Finish timing this method
    auto finish = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(finish - start);

    // Print results
    if (err.extended_error::code != boost::system::errc::success)
        std::cerr << "TIMESTAMPTZ TEXT results in Error: " << err.code.what() << ": " << err.diag.message() << " (in " << duration << ")" << std::endl;
    else
        std::cout << "TIMESTAMPTZ TEXT select result: " << std::format("{0:%F} {0:%T} {0:%Oz}", select_vec[0].tsz)
                    << " (in " << duration << ")" << std::endl;
}

static asio::awaitable<void> timestamptz_binary_example(connection& conn)
{
    // Start timing this operation
    auto start = std::chrono::high_resolution_clock::now();

    // Compose our request
    request req;
    req.add_prepare("SELECT $1::text::timestamptz as tsz", {"timestamptz_bintest"} )
        .add_execute("timestamptz_bintest", {"2026-02-08 12:34:23.43535+05:00"}, request::param_format::text, protocol::format_code::binary, 1);

    // Structures to parse the response into
    std::vector<timestamptz_row> select_vec;
    response res{into(select_vec)};

    auto [err] = co_await conn.async_exec(req, res, asio::as_tuple);

    // Finish timing this method
    auto finish = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(finish - start);

    // Print results
    if (err.extended_error::code != boost::system::errc::success)
        std::cerr << "TIMESTAMPTZ BINARY Error: " << err.code.what() << ": " << err.diag.message() << " (in " << duration << ")" << std::endl;
    else
        std::cout << "TIMESTAMPTZ BINARY select result: " << std::format("{0:%F} {0:%T} {0:%Oz}", select_vec[0].tsz)
                    <<  " (in " << duration << ")" << std::endl;
}


// INTERVAL to pg_interval (TEXT)
static asio::awaitable<void> interval_text_example(connection& conn)
{
    // Start timing this operation
    auto start = std::chrono::high_resolution_clock::now();

    // Compose our request
    request req;
    req.add_query("SELECT INTERVAL '5Years 1Months 3Days 7Hours 4Minutes 10seconds' as iv", {});

    // Structures to parse the response into
    std::vector<interval_row> select_vec;
    response res{into(select_vec)};

    auto [err] = co_await conn.async_exec(req, res, asio::as_tuple);

    // Finish timing this method
    auto finish = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(finish - start);

    // Print results
    if (err.extended_error::code != boost::system::errc::success)
        std::cerr << "INTERVAL TEXT results in Error: " << err.code.what() << ": " << err.diag.message() << " (in " << duration << ")" << std::endl;
    else
        std::cout << "INTERVAL TEXT select result: " << select_vec[0].iv.months << " month(s) " << select_vec[0].iv.days << " day(s) " << std::format("{0:%T}", select_vec[0].iv.time)
                    << " (in " << duration << ")" << std::endl;
}

// INTERVAL example (BINARY)
static asio::awaitable<void> interval_binary_example(connection& conn)
{
    // Start timing this operation
    auto start = std::chrono::high_resolution_clock::now();

    // Compose our request
    request req;
    req.add_prepare("SELECT $1::text::interval as iv", {"interval_bintest"} )
        .add_execute("interval_bintest", {"1977 years 6 months 21 days 12:34:23.43535"}, request::param_format::text, protocol::format_code::binary, 1);

    // Structures to parse the response into
    std::vector<interval_row> select_vec;
    response res{into(select_vec)};

    auto [err] = co_await conn.async_exec(req, res, asio::as_tuple);

    // Finish timing this method
    auto finish = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(finish - start);

    // Print results
    if (err.extended_error::code != boost::system::errc::success)
        std::cerr << "INTERVAL BINARY Error: " << err.code.what() << ": " << err.diag.message() << " (in " << duration << ")" << std::endl;
    else
        std::cout << "INTERVAL BINARY select result: "
                    << select_vec[0].iv.months << " month(s) "
                    << select_vec[0].iv.days << " day(s) "
                    << std::format("{0:%T}", select_vec[0].iv.time)
                    <<  " (in " << duration << ")" << std::endl;
}


static asio::awaitable<void> co_main()
{
    // Create a connection
    connection conn{co_await asio::this_coro::executor};

    // Connect
    co_await conn.async_connect(
        {.hostname = "localhost", .username = "henry", .password = "", .database = "postgres"}
    );
    std::cout << "Startup complete\n";

    co_await date_text_example(conn);
    co_await date_binary_example(conn);

    co_await time_text_example(conn);
    co_await time_binary_example(conn);

    co_await timetz_text_example(conn);
    co_await timetz_binary_example(conn);

    co_await timestamp_text_example(conn);
    co_await timestamp_binary_example(conn);

    co_await timestamptz_text_example(conn);
    co_await timestamptz_binary_example(conn);

    co_await interval_text_example(conn);
    co_await interval_binary_example(conn);

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