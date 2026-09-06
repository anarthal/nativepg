//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/core/lightweight_test.hpp>

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

#include "nativepg/protocol/bind.hpp"
#include "nativepg/protocol/close.hpp"
#include "nativepg/protocol/command_complete.hpp"
#include "nativepg/protocol/data_row.hpp"
#include "nativepg/protocol/describe.hpp"
#include "nativepg/protocol/empty_query_response.hpp"
#include "nativepg/protocol/execute.hpp"
#include "nativepg/protocol/notice_error.hpp"
#include "nativepg/protocol/parse.hpp"
#include "nativepg/responses/any_request_message.hpp"

using namespace nativepg;
using kind = any_request_message::kind;

namespace {

//
// Sample messages. Messages carrying data use values that we can recognize,
// so we can check that the accessors return the object we stored.
// The parsing views are only inspected through size(), so we can build them
// without serializing actual message contents.
//
protocol::command_complete sample_command_complete() { return {.tag = "SELECT 1"}; }
protocol::data_row sample_data_row() { return {.columns = {3u, {}}}; }
protocol::parameter_description sample_parameter_description()
{
    return {.parameter_type_oids = {nullptr, 4u}};
}
protocol::row_description sample_row_description() { return {.field_descriptions = {5u, {}}}; }
protocol::error_response sample_error_response() { return {{.sqlstate = "42P01"}}; }

//
// Constructors
//
void test_ctor_bind_complete()
{
    any_request_message msg{protocol::bind_complete{}};
    BOOST_TEST(msg.type() == kind::bind_complete);
}

void test_ctor_close_complete()
{
    any_request_message msg{protocol::close_complete{}};
    BOOST_TEST(msg.type() == kind::close_complete);
}

void test_ctor_command_complete()
{
    any_request_message msg{sample_command_complete()};
    BOOST_TEST(msg.type() == kind::command_complete);
}

void test_ctor_data_row()
{
    any_request_message msg{sample_data_row()};
    BOOST_TEST(msg.type() == kind::data_row);
}

void test_ctor_parameter_description()
{
    any_request_message msg{sample_parameter_description()};
    BOOST_TEST(msg.type() == kind::parameter_description);
}

void test_ctor_row_description()
{
    any_request_message msg{sample_row_description()};
    BOOST_TEST(msg.type() == kind::row_description);
}

void test_ctor_empty_query_response()
{
    any_request_message msg{protocol::empty_query_response{}};
    BOOST_TEST(msg.type() == kind::empty_query_response);
}

void test_ctor_portal_suspended()
{
    any_request_message msg{protocol::portal_suspended{}};
    BOOST_TEST(msg.type() == kind::portal_suspended);
}

void test_ctor_error_response()
{
    any_request_message msg{sample_error_response()};
    BOOST_TEST(msg.type() == kind::error_response);
}

void test_ctor_parse_complete()
{
    any_request_message msg{protocol::parse_complete{}};
    BOOST_TEST(msg.type() == kind::parse_complete);
}

void test_ctor_message_skipped()
{
    any_request_message msg = any_request_message::message_skipped();
    BOOST_TEST(msg.type() == kind::message_skipped);
}

//
// get_xxx accessors. Only success conditions, since a kind mismatch is a precondition violation
//
void test_get_command_complete()
{
    any_request_message msg{sample_command_complete()};
    BOOST_TEST_EQ(msg.get_command_complete().tag, "SELECT 1");
}

void test_get_data_row()
{
    any_request_message msg{sample_data_row()};
    BOOST_TEST_EQ(msg.get_data_row().columns.size(), 3u);
}

void test_get_parameter_description()
{
    any_request_message msg{sample_parameter_description()};
    BOOST_TEST_EQ(msg.get_parameter_description().parameter_type_oids.size(), 4u);
}

void test_get_row_description()
{
    any_request_message msg{sample_row_description()};
    BOOST_TEST_EQ(msg.get_row_description().field_descriptions.size(), 5u);
}

void test_get_error_response()
{
    any_request_message msg{sample_error_response()};
    BOOST_TEST_EQ(msg.get_error_response().sqlstate.value(), "42P01");
}

//
// as_xxx accessors
//
struct named_message
{
    const char* name;
    any_request_message msg;
};

// One message per kind
std::vector<named_message> all_messages()
{
    return {
        {"bind_complete",         protocol::bind_complete{}              },
        {"close_complete",        protocol::close_complete{}             },
        {"command_complete",      sample_command_complete()              },
        {"data_row",              sample_data_row()                      },
        {"parameter_description", sample_parameter_description()         },
        {"row_description",       sample_row_description()               },
        {"empty_query_response",  protocol::empty_query_response{}       },
        {"portal_suspended",      protocol::portal_suspended{}           },
        {"error_response",        sample_error_response()                },
        {"parse_complete",        protocol::parse_complete{}             },
        {"message_skipped",       any_request_message::message_skipped()},
    };
}

// Checks that the given accessor throws for every kind other than the expected one
template <class Accessor>
void check_other_kinds_throw(kind expected, Accessor accessor)
{
    for (const auto& elm : all_messages())
    {
        if (elm.msg.type() == expected)
            continue;

        bool threw = false;
        try
        {
            accessor(elm.msg);
        }
        catch (const std::invalid_argument&)
        {
            threw = true;
        }
        if (!BOOST_TEST(threw))
            std::cerr << "  while accessing a " << elm.name << " message" << std::endl;
    }
}

void test_as_command_complete()
{
    any_request_message msg{sample_command_complete()};
    BOOST_TEST_EQ(msg.as_command_complete().tag, "SELECT 1");

    check_other_kinds_throw(kind::command_complete, [](const any_request_message& m) {
        m.as_command_complete();
    });
}

void test_as_data_row()
{
    any_request_message msg{sample_data_row()};
    BOOST_TEST_EQ(msg.as_data_row().columns.size(), 3u);

    check_other_kinds_throw(kind::data_row, [](const any_request_message& m) { m.as_data_row(); });
}

void test_as_parameter_description()
{
    any_request_message msg{sample_parameter_description()};
    BOOST_TEST_EQ(msg.as_parameter_description().parameter_type_oids.size(), 4u);

    check_other_kinds_throw(kind::parameter_description, [](const any_request_message& m) {
        m.as_parameter_description();
    });
}

void test_as_row_description()
{
    any_request_message msg{sample_row_description()};
    BOOST_TEST_EQ(msg.as_row_description().field_descriptions.size(), 5u);

    check_other_kinds_throw(kind::row_description, [](const any_request_message& m) {
        m.as_row_description();
    });
}

void test_as_error_response()
{
    any_request_message msg{sample_error_response()};
    BOOST_TEST_EQ(msg.as_error_response().sqlstate.value(), "42P01");

    check_other_kinds_throw(kind::error_response, [](const any_request_message& m) {
        m.as_error_response();
    });
}

}  // namespace

int main()
{
    test_ctor_bind_complete();
    test_ctor_close_complete();
    test_ctor_command_complete();
    test_ctor_data_row();
    test_ctor_parameter_description();
    test_ctor_row_description();
    test_ctor_empty_query_response();
    test_ctor_portal_suspended();
    test_ctor_error_response();
    test_ctor_parse_complete();
    test_ctor_message_skipped();

    test_get_command_complete();
    test_get_data_row();
    test_get_parameter_description();
    test_get_row_description();
    test_get_error_response();

    test_as_command_complete();
    test_as_data_row();
    test_as_parameter_description();
    test_as_row_description();
    test_as_error_response();

    return boost::report_errors();
}
