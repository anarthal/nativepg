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

#include <chrono>
#include <exception>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "nativepg/connection.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/request.hpp"
#include "nativepg/response.hpp"
#include "nativepg/types/nullable.hpp"

namespace asio = boost::asio;
using namespace nativepg;

struct test_row
{
    std::string title;

    std::optional<bool> nt_ob;
    std::optional<bool> vt_ob;

    std::optional<double> nt_f8;
    std::optional<double> vt_f8;

    std::optional<std::string> nt_t;
    std::optional<std::string> vt_t;
};
BOOST_DESCRIBE_STRUCT(test_row, (), (title, nt_ob, vt_ob, nt_f8, vt_f8, nt_t, vt_t));

template <typename T>
static inline std::ostream& operator<<(std::ostream& os, const std::optional<T>& opt)
{
    if (!opt.has_value())
    {
        os << "<null>";
    }
    else
    {
        os << opt.value();
    }

    return os;
}

static void print_err(const char* prefix, const std::error_code err, const diagnostics& diag)
{
    std::cerr << prefix << ": " << err << ": " << err.message();
    if (!diag.message().empty())
        std::cerr << ": " << diag.message();
    std::cerr << '\n';
}

static asio::awaitable<void> nullable_text_example(connection& conn)
{
    // Start timing this operation
    auto start = std::chrono::high_resolution_clock::now();

    // Compose our request
    request req;
    req.add_query(
        R"sql(
SELECT  'Nullable Test values' as title,
        NULL::bool as nt_ob,
        true as vt_ob,
        NULL::float8 nt_f8,
        21.1977::float8 vt_f8,
        NULL::text nt_t,
        'Value Test text'::text vt_t
    )sql",
        {}
    );

    // Structures to parse the response into
    std::vector<test_row> select_vec;
    response res{into(select_vec)};

    auto [err] = co_await conn.async_exec(req, res, asio::as_tuple);

    // Finish timing this method
    auto finish = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(finish - start);

    // Print results
    if (err.extended_error::code != boost::system::errc::success)
        std::cerr << "NULLABLE TEXT operation results in Error: " << err.code.what() << ": " << err.diag.message()
                  << " (in " << duration << ")" << std::endl;
    else
    {
        std::cout << std::boolalpha;
        std::cout << "NULLABLE TEXT   select result: (in " << duration << ")" << std::endl;
        for (const auto& [title, nt_ob, vt_ob, nt_f8, vt_f8, nt_t, vt_t] : select_vec)
        {
            std::cout << " | " << title << " | " << nt_ob << " | " << vt_ob << " | " << nt_f8 << " | " << vt_f8 << " | "
                      << nt_t << " | " << vt_t << std::endl;
        }
        std::cout << std::endl;
    }
}

static asio::awaitable<void> nullable_binary_example(connection& conn)
{
    // Start timing this operation
    auto start = std::chrono::high_resolution_clock::now();

    statement<std::string_view> select_stmt{"nullable_bintest"};

    // Compose our request
    request req{};
    req.add_query(
        R"sql(
        SELECT  $1 as title,
                 NULLIF($2::text, 'NULL')::bool as nt_ob,
                 NULLIF($3::text, 'NULL')::bool as vt_ob,
                 NULLIF($4::text, 'NULL')::float8 as nt_f8,
                 NULLIF($5::text, 'NULL')::float8 as vt_f8,
                 NULLIF($6::text, 'NULL')::text as nt_t,
                 NULLIF($7::text, 'NULL')::text as vt_t
        )sql", { "Nullable Test values", "NULL", "true", "NULL", "21.1977", "NULL", "Value Test text"}
            , request::param_format::text, protocol::format_code::binary)
    ;

    std::vector<test_row> select_vec;
    if (auto [err] = co_await conn.async_exec(req, into(select_vec), asio::as_tuple);
        err.extended_error::code != boost::system::errc::success)
    {
        print_err("Error ", err.code, err.diag);
        co_return;
    }

    // Finish timing this method
    auto finish = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(finish - start);


    std::cout << "NULLABLE BINARY select results: " << " (in " << duration << ")" << std::endl << std::boolalpha << " | " << select_vec[0].title << " | " << select_vec[0].nt_ob << " | " << select_vec[0].vt_ob << " | "
              << select_vec[0].nt_f8 << " | " << select_vec[0].vt_f8 << " | " << select_vec[0].nt_t << " | "
              << select_vec[0].vt_t;
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

    co_await nullable_text_example(conn);
    co_await nullable_binary_example(conn);

    std::cout << "\nDone\n";
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
