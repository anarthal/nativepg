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
#include "nativepg/udt/user_defined_type.hpp"

// CREATE TYPE zoning_type AS ENUM ('Residential', 'Commercial', 'Industrial', 'Agricultural');
enum zoning_type
{
    Residential,
    Commercial,
    Industrial,
    Agricultural
};




namespace nativepg::udt {

template <>
class user_defined_type<zoning_type>
{
protected:
    types::type_info db_ti_;

public:
    explicit user_defined_type(const types::type_info& ti) : db_ti_(ti) {}
    virtual ~user_defined_type() = default;


    boost::system::error_code is_compatible(const protocol::field_description& desc)
    {
        if (db_ti_.type_oid == desc.type_oid && db_ti_.type_name == "zoning_type")
            return boost::system::error_code{};

        return client_errc::incompatible_field_type;
    }

    boost::system::error_code parse(
        const field_view from,
        const protocol::field_description& desc,
        zoning_type& to)
    {
        if (desc.fmt_code == protocol::format_code::text)
        {
            auto sv = from.data_str();
            if (sv == "Residential")
                to = zoning_type::Residential;
            else if (sv == "Commercial")
                to = zoning_type::Commercial;
            else if (sv == "Industrial")
                to = zoning_type::Industrial;
            else if (sv == "Agricultural")
                to = zoning_type::Agricultural;
        }
        else
            return client_errc::incompatible_field_type;

        return boost::system::error_code{};
    }
};

}

#include "nativepg/response.hpp"

namespace asio = boost::asio;
using namespace nativepg;

struct test_row
{
    std::string title;
    zoning_type zt1;
    zoning_type zt2;
    zoning_type zt3;
    zoning_type zt4;
};
BOOST_DESCRIBE_STRUCT(test_row, (), (title, zt1, zt2, zt3, zt4))


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

    for (auto& t: conn.ctx().types())
    {
        std::cout << t.type_oid << " | " << t.schema_name << " | " << t.type_name << " | " << t.extension_name << " | " << t.array_element_oid << " | " << t.schema_path_seqno << " | " << t.storage_bytes << std::endl;
    }

    request req{};
    req.add_query(R"sql(
        SELECT
            'My House' AS title,
            'Residential'::zoning_type AS zt1,
            'Commercial'::zoning_type AS zt2,
            'Industrial'::zoning_type AS zt3,
            'Agricultural'::zoning_type AS zt4
        )sql", {});

    auto result = std::vector<test_row>{};
    auto [err2] = co_await conn.async_exec(req, into(result), asio::as_tuple);

    for (auto& row: result)
    {
        std::cout << row.title << " | " << row.zt1 << " | " << row.zt2 << " | " << row.zt3 << " | " << row.zt4 << std::endl;
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
