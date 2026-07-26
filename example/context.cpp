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
#include <iostream>
#include <string_view>
#include <vector>

#include "nativepg/connection.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/request.hpp"
#include "nativepg/response.hpp"
#include "nativepg/context/db_info.hpp"
#include "nativepg/context/type_info.hpp"

namespace asio = boost::asio;
using namespace nativepg;

static void print_err(const char* prefix, const extended_error& err)
{
    std::cout << prefix << err.code.what() << ": " << err.diag.message() << '\n';
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

    // Load contexts
    auto [err] = co_await conn.async_load_contexts<context::db_info_tag, context::type_info_tag>(boost::asio::as_tuple);
    std::cout << "Contexts loaded\n";

    auto db_info = context::make_context<context::db_info_tag>();
    if (!conn.context().resolve<context::db_info_tag>(db_info))
    {
        std::cout << "Server version: " << db_info.server_version << std::endl;
    }

    auto types_info = context::make_context<context::type_info_tag>();
    if (!conn.context().resolve<context::type_info_tag>(types_info))
    {
        for (const auto& type_info : types_info)
        {
            std::cout << "Type: " << type_info.type_name << ", OID: " << type_info.type_oid << std::endl;
        }
    }

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