//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/assert/source_location.hpp>
#include <boost/core/lightweight_test.hpp>
#include <boost/core/span.hpp>
#include <boost/describe/class.hpp>
#include <boost/describe/operators.hpp>
#include <boost/system/detail/error_code.hpp>
#include <boost/system/error_code.hpp>

#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

#include "../src/serialization_context.hpp"  // TODO
#include "nativepg/client_errc.hpp"
#include "nativepg/protocol/bind.hpp"
#include "nativepg/protocol/command_complete.hpp"
#include "nativepg/protocol/common.hpp"
#include "nativepg/protocol/data_row.hpp"
#include "nativepg/protocol/describe.hpp"
#include "nativepg/protocol/parse.hpp"
#include "nativepg/response.hpp"

using namespace nativepg;
using namespace boost::describe::operators;
using boost::system::error_code;
using protocol::format_code;

namespace {

struct owning_row_description
{
    std::vector<unsigned char> data;
    protocol::row_description msg;

    owning_row_description(
        std::initializer_list<protocol::field_description> descrs,
        boost::source_location loc = BOOST_CURRENT_LOCATION
    )
    {
        // Create the serialized message
        protocol::detail::serialization_context ctx(data);
        ctx.add_integral(static_cast<std::int16_t>(descrs.size()));
        for (const auto& desc : descrs)
        {
            ctx.add_string(desc.name);
            ctx.add_integral(desc.table_oid);
            ctx.add_integral(desc.column_attribute);
            ctx.add_integral(desc.type_oid);
            ctx.add_integral(desc.type_length);
            ctx.add_integral(desc.type_modifier);
            ctx.add_integral(static_cast<std::int16_t>(desc.fmt_code));
        }
        if (!BOOST_TEST_EQ(ctx.error(), error_code()))
        {
            std::cerr << "Called from " << loc << std::endl;
            exit(1);
        }

        // Now create the view to parse it
        auto ec = protocol::parse(data, msg);
        if (!BOOST_TEST_EQ(ec, error_code()))
        {
            std::cerr << "Called from " << loc << std::endl;
            exit(1);
        }
    }

    operator protocol::row_description() const { return msg; }
};

struct owning_data_row
{
    std::vector<unsigned char> data;
    protocol::data_row msg;

    owning_data_row(
        std::initializer_list<std::string_view> values,
        boost::source_location loc = BOOST_CURRENT_LOCATION
    )
    {
        // Create the serialized message
        protocol::detail::serialization_context ctx(data);
        ctx.add_integral(static_cast<std::int16_t>(values.size()));
        for (const auto value : values)
        {
            ctx.add_integral(static_cast<std::int32_t>(value.size()));
            ctx.add_bytes(value);
        }
        if (!BOOST_TEST_EQ(ctx.error(), error_code()))
        {
            std::cerr << "Called from " << loc << std::endl;
            exit(1);
        }

        // Now create the view to parse it
        auto ec = protocol::parse(data, msg);
        if (!BOOST_TEST_EQ(ec, error_code()))
        {
            std::cerr << "Called from " << loc << std::endl;
            exit(1);
        }
    }

    operator protocol::data_row() const { return msg; }
};

struct user
{
    std::int32_t id;
    std::string name;
};
BOOST_DESCRIBE_STRUCT(user, (), (id, name))

// Simple queries work (vs. extended protocol queries)
void test_simple_query()
{
    // Setup
    std::vector<user> users;
    auto cb = into(users);
    owning_row_description descrs({
        // clang-format off
        {.name = "id",   .table_oid = 0, .column_attribute = 1, .type_oid = 23, .type_length = -1, .type_modifier = -1, .fmt_code = format_code::text},
        {.name = "name", .table_oid = 0, .column_attribute = 1, .type_oid = 25, .type_length = -1, .type_modifier = -1, .fmt_code = format_code::text},
        // clang-format on
    });

    // Messages
    BOOST_TEST_EQ(cb(descrs), error_code(client_errc::needs_more));
    BOOST_TEST_EQ(cb(owning_data_row({"42", "perico"})), error_code(client_errc::needs_more));
    BOOST_TEST_EQ(cb(protocol::command_complete{}), error_code());
}

// The usual extended query flow works
void test_query()
{
    // Setup
    std::vector<user> users;
    auto cb = into(users);
    owning_row_description descrs({
        // clang-format off
        {.name = "id",   .table_oid = 0, .column_attribute = 1, .type_oid = 23, .type_length = -1, .type_modifier = -1, .fmt_code = format_code::text},
        {.name = "name", .table_oid = 0, .column_attribute = 1, .type_oid = 25, .type_length = -1, .type_modifier = -1, .fmt_code = format_code::text},
        // clang-format on
    });

    // Messages
    BOOST_TEST_EQ(cb(protocol::parse_complete{}), error_code(client_errc::needs_more));
    BOOST_TEST_EQ(cb(protocol::bind_complete{}), error_code(client_errc::needs_more));
    BOOST_TEST_EQ(cb(descrs), error_code(client_errc::needs_more));
    BOOST_TEST_EQ(cb(owning_data_row({"42", "perico"})), error_code(client_errc::needs_more));
    BOOST_TEST_EQ(cb(owning_data_row({"50", "pepe"})), error_code(client_errc::needs_more));
    BOOST_TEST_EQ(cb(protocol::command_complete{}), error_code());

    // TODO: check rows
}

}  // namespace

int main()
{
    test_simple_query();
    test_query();

    return boost::report_errors();
}