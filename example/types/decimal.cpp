//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#define NATIVEPG_USE_DECIMAL_TYPES 1

#include <boost/asio/as_tuple.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/address_v4.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/decimal.hpp>
#include <boost/describe/class.hpp>

#include <chrono>
#include <exception>
#include <iomanip>
#include <iostream>
#include <string_view>
#include <vector>

#include "nativepg/connection.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/request.hpp"
#include "nativepg/response.hpp"
#include "nativepg/types/decimal.hpp"

namespace asio = boost::asio;
namespace bd = boost::decimal;
using namespace nativepg;

struct test_row
{
    std::string title;
    bd::decimal32_t d32;
    bd::decimal64_t d64;
    bd::decimal128_t d128;
};
BOOST_DESCRIBE_STRUCT(test_row, (), (d32, d64, d128))

static void print_err(const char* prefix, std::error_code err, const diagnostics& diag)
{
    std::cerr << prefix << ": " << err << ": " << err.message();
    if (!diag.message().empty())
        std::cerr << ": " << diag.message();
    std::cerr << '\n';
}

static asio::awaitable<void> decimal_text_example(connection& conn)
{
    // Start timing this operation
    auto start = std::chrono::high_resolution_clock::now();

    // Compose our request
    request req;
    req.add_query(
        R"sql(
SELECT
    'Test values'              as title,
    '11.77'::decimal(7, 2)  as d32,
    '11.21061977'::decimal(16, 2) as d64,
    '11.21061977'::decimal(34, 2) as d128
UNION ALL
SELECT
    'Minimum values' as title,
    '-9999.99'::decimal(7, 2) as d32,
    '-9999999999999.99'::decimal(16, 2) as d64,
    '-9999999999999999999999999999999.99'::decimal(34, 2) as d128
UNION ALL
SELECT
    'Maximum values' as title,
    '9999.99'::decimal(7, 2) as d32,
    '9999999999999.99'::decimal(16, 2) as d64,
    '9999999999999999999999999999999.99'::decimal(34, 2) as d128
   )sql",
        {}
    );

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
        std::cerr << "NUMERIC TEXT operation results in Error: " << err.code.what() << ": "
                  << err.diag.message() << " (in " << duration << ")" << std::endl;
    else
    {
        std::cout << std::boolalpha;
        std::cout << "NUMERIC TEXT   select result: (in " << duration << ")" << std::endl;
        for (const auto& row : select_vec)
        {
            std::cout << " | " << row.title << " | " << row.d32 << " | " << row.d64 << " | " << row.d128
                      << std::endl;
        }
        std::cout << std::endl;
    }
}

template <typename T>
static asio::awaitable<void> execute_and_print_binary_response(
    connection& conn,
    statement<T>& stmnt,
    std::initializer_list<parameter_ref> params
)
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
        std::cerr << "NUMERIC BINARY operation results in Error: " << err.code.what() << ": "
                  << err.diag.message() << std::endl;
    else
    {
        std::cout << std::boolalpha;
        std::cout << "NUMERIC BINARY select result: | " << select_vec[0].title << " | " << select_vec[0].d32
                  << " | " << select_vec[0].d64 << " | " << select_vec[0].d128 << std::endl;
    }
}

static asio::awaitable<void> decimal_binary_example(connection& conn)
{
    // Start timing this operation
    auto start = std::chrono::high_resolution_clock::now();
    diagnostics diag;

    statement<std::string_view> select_stmt{"decimal_bintest"};

    // Compose our request
    request req{false};  // Turns off autosync for better efficiency
    req.add_prepare(
        R"sql(
        SELECT  $1 as title,
                 $2::text::decimal(7, 2) as d32,
                 $3::text::decimal(16, 2) as d64,
                 $4::text::decimal(34, 2) as d128
    )sql",
        select_stmt
    );
    req.add_sync();

    // Actually prepare the statements
    response res{check_parse()};
    auto [err] = co_await conn.async_exec(req, res, asio::as_tuple);
    if (err.extended_error::code != boost::system::errc::success)
    {
        print_err("Error preparing", err.code, diag);
        co_return;
    }

    co_await execute_and_print_binary_response(conn, select_stmt, {"Test values", "11.21", "11.21", "11.21"});
    co_await execute_and_print_binary_response(
        conn,
        select_stmt,
        {"Minimum values", "-9999.99", "-9999999999999.99", "-9999999999999999999999999999999.99"}
    );
    co_await execute_and_print_binary_response(
        conn,
        select_stmt,
        {"Maximum values", "9999.99", "9999999999999.99", "9999999999999999999999999999999.99"}
    );

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

    co_await decimal_text_example(conn);
    co_await decimal_binary_example(conn);

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
