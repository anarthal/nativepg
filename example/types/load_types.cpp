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
#include "nativepg/types/base.hpp"

namespace asio = boost::asio;
using namespace nativepg;

static void print_err(const char* prefix, const std::error_code err, const diagnostics& diag)
{
    std::cerr << prefix << ": " << err << ": " << err.message();
    if (!diag.message().empty())
        std::cerr << ": " << diag.message();
    std::cerr << '\n';
}

static asio::awaitable<void> load_types_example(connection& conn)
{
    // Start timing this operation
    auto start = std::chrono::high_resolution_clock::now();

    auto [err] = co_await conn.async_load_types(asio::as_tuple);

    if (err.code)
    {
        print_err("load_types_example", err.code, err.diag);
        co_return;
    }

    auto geometry_info1 = conn.types().find_by_type_name("geometry");
    auto geometry_info2 = conn.types().find_by_oid(geometry_info1.type_oid);

    if (geometry_info2.type_oid != geometry_info1.type_oid || geometry_info2.type_name != geometry_info1.type_name)
    {
        std::cerr << "Type info mismatch for 'geometry' type\n";
        co_return;
    }

    for (auto& t: conn.types())
    {
        std::cout << t.type_oid << " | " << t.schema_name << " | " << t.type_name << " | " << t.extension_name << " | " << t.array_element_oid << " | " << t.schema_path_seqno << " | " << t.storage_bytes << std::endl;
    }

    // Finish timing this method
    auto finish = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(finish - start);

    // Print results
    if (err.extended_error::code != boost::system::errc::success)
        std::cerr << "LOAD TYPES operation results in Error: " << err.code.what() << ": " << err.diag.message()
                  << " (in " << duration << ")" << std::endl;
    else
    {
        std::cout << std::boolalpha;
        std::cout << "LOAD TYPES result: (in " << duration << ")" << std::endl;

        std::cout << std::endl;
    }
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

    co_await load_types_example(conn);

    std::cout << "Done\n";
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
