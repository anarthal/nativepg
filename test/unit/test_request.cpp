//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/assert/source_location.hpp>
#include <boost/core/lightweight_test.hpp>
#include <boost/system/error_code.hpp>

#include <cstdint>
#include <initializer_list>
#include <ostream>
#include <source_location>
#include <string_view>

#include "nativepg/protocol/common.hpp"
#include "nativepg/request.hpp"
#include "test_utils.hpp"

using namespace nativepg;
using detail::request_msg_type;

namespace nativepg::detail {

std::ostream& operator<<(std::ostream& os, request_msg_type type)
{
    switch (type)
    {
    case request_msg_type::bind: return os << "bind";
    case request_msg_type::close: return os << "close";
    case request_msg_type::describe: return os << "describe";
    case request_msg_type::execute: return os << "execute";
    case request_msg_type::flush: return os << "flush";
    case request_msg_type::parse: return os << "parse";
    case request_msg_type::query: return os << "query";
    case request_msg_type::sync: return os << "sync";
    default: return os << "<unknown request_msg_type>";
    }
}

}  // namespace nativepg::detail

namespace {

void check_payload(
    const request& req,
    std::initializer_list<unsigned char> expected,
    std::source_location loc = std::source_location::current()
)
{
    test::context_frame frame{loc};
    NATIVEPG_TEST_CONT_EQ(req.payload(), expected);
}

void check_messages(
    const request& req,
    std::initializer_list<request_msg_type> expected,
    std::source_location loc = std::source_location::current()
)
{
    test::context_frame frame{loc};
    NATIVEPG_TEST_CONT_EQ(detail::request_access::messages(req), expected);
}

// Simple query
void test_simple_query()
{
    request req;
    req.add_simple_query("select 1;");

    check_payload(
        req,
        {0x51, 0x00, 0x00, 0x00, 0x0e, 0x73, 0x65, 0x6c, 0x65, 0x63, 0x74, 0x20, 0x31, 0x3b, 0x00}
    );
    check_messages(req, {request_msg_type::query});
}

// Query with parameters
void test_query()
{
    request req;
    req.add_query("SELECT $1, $2", {std::int32_t(42), "value"});

    // clang-format off
    check_payload(req, {
        // Parse
        0x50, 0x00, 0x00, 0x00, 0x1d, 0x00, 0x53, 0x45, 0x4c, 0x45,
        0x43, 0x54, 0x20, 0x24, 0x31, 0x2c, 0x20, 0x24, 0x32, 0x00,
        0x00, 0x02, 0x00, 0x00, 0x00, 0x17, 0x00, 0x00, 0x00, 0x19,

        // Bind
        0x42, 0x00, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x00, 0x01, 0x00,
        0x01, 0x00, 0x02, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
        0x2a, 0x00, 0x00, 0x00, 0x05, 0x76, 0x61, 0x6c, 0x75, 0x65,
        0x00, 0x00,

        // Describe
        0x44, 0x00, 0x00, 0x00, 0x06, 0x50, 0x00,

        // Execute
        0x45, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00,

        // Sync
        0x53, 0x00, 0x00, 0x00, 0x04
    });
    // clang-format on

    check_messages(
        req,
        {
            request_msg_type::parse,
            request_msg_type::bind,
            request_msg_type::describe,
            request_msg_type::execute,
            request_msg_type::sync,
        }
    );
}

void test_query_text()
{
    request req;
    req.add_query("SELECT $1, $2", {std::int32_t(42), "value"}, request::param_format::text);

    // clang-format off
    check_payload(req, {
        // Parse
        0x50, 0x00, 0x00, 0x00, 0x1d, 0x00, 0x53, 0x45, 0x4c, 0x45,
        0x43, 0x54, 0x20, 0x24, 0x31, 0x2c, 0x20, 0x24, 0x32, 0x00,
        0x00, 0x02, 0x00, 0x00, 0x00, 0x17, 0x00, 0x00, 0x00, 0x19,

        // Bind
        0x42, 0x00, 0x00, 0x00, 0x1b, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x02, 0x00, 0x00, 0x00, 0x02, 0x34, 0x32, 0x00, 0x00, 0x00,
        0x05, 0x76, 0x61, 0x6c, 0x75, 0x65, 0x00, 0x00,

        // Describe
        0x44, 0x00, 0x00, 0x00, 0x06, 0x50, 0x00,

        // Execute
        0x45, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00,

        // Sync
        0x53, 0x00, 0x00, 0x00, 0x04
    });
    // clang-format on

    check_messages(
        req,
        {
            request_msg_type::parse,
            request_msg_type::bind,
            request_msg_type::describe,
            request_msg_type::execute,
            request_msg_type::sync,
        }
    );
}

// TODO: max num rows, result format codes

// Prepare
void test_prepare_untyped()
{
    request req;
    req.add_prepare("SELECT $1, $2", "myname");

    // clang-format off
    check_payload(req, {
        // Parse
        0x50, 0x00, 0x00, 0x00, 0x1b, 0x6d, 0x79, 0x6e,
        0x61, 0x6d, 0x65, 0x00, 0x53, 0x45, 0x4c, 0x45,
        0x43, 0x54, 0x20, 0x24, 0x31, 0x2c, 0x20, 0x24,
        0x32, 0x00, 0x00, 0x00,

        // Sync
        0x53, 0x00, 0x00, 0x00, 0x04,
    });
    // clang-format on

    check_messages(req, {request_msg_type::parse, request_msg_type::sync});
}

void test_prepare_typed()
{
    statement<std::int32_t, std::string_view> stmt{"myname"};
    request req;
    req.add_prepare("SELECT $1, $2", stmt);

    // clang-format off
    check_payload(req, {
        // Parse
        0x50, 0x00, 0x00, 0x00, 0x23, 0x6d, 0x79, 0x6e, 0x61, 0x6d,
        0x65, 0x00, 0x53, 0x45, 0x4c, 0x45, 0x43, 0x54, 0x20, 0x24,
        0x31, 0x2c, 0x20, 0x24, 0x32, 0x00, 0x00, 0x02, 0x00, 0x00,
        0x00, 0x17, 0x00, 0x00, 0x00, 0x19,

        // Sync
        0x53, 0x00, 0x00, 0x00, 0x04,
    });
    // clang-format on

    check_messages(req, {request_msg_type::parse, request_msg_type::sync});
}

// Execute
void test_execute_untyped()
{
    request req;
    req.add_execute("myname", {42, "value"});  // will use text by default

    // clang-format off
    check_payload(req, {
        // Bind
        0x42, 0x00, 0x00, 0x00, 0x21, 0x00, 0x6d, 0x79, 0x6e, 0x61,
        0x6d, 0x65, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
        0x02, 0x34, 0x32, 0x00, 0x00, 0x00, 0x05, 0x76, 0x61, 0x6c,
        0x75, 0x65, 0x00, 0x00,

        // Describe
        0x44, 0x00, 0x00, 0x00, 0x06, 0x50, 0x00,

        // Execute
        0x45, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00,

        // Sync
        0x53, 0x00, 0x00, 0x00, 0x04
    });
    // clang-format on

    check_messages(
        req,
        {
            request_msg_type::bind,
            request_msg_type::describe,
            request_msg_type::execute,
            request_msg_type::sync,
        }
    );
}

void test_execute_typed()
{
    statement<std::int32_t, std::string_view> stmt{"myname"};
    request req;
    req.add_execute(stmt, 42, "value");

    // clang-format off
    check_payload(req, {
        // Bind
        0x42, 0x00, 0x00, 0x00, 0x25, 0x00, 0x6d, 0x79, 0x6e, 0x61,
        0x6d, 0x65, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x02, 0x00,
        0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x2a, 0x00, 0x00, 0x00,
        0x05, 0x76, 0x61, 0x6c, 0x75, 0x65, 0x00, 0x00,

        // Describe
        0x44, 0x00, 0x00, 0x00, 0x06, 0x50, 0x00,

        // Execute
        0x45, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00,

        // Sync
        0x53, 0x00, 0x00, 0x00, 0x04
    });
    // clang-format on

    check_messages(
        req,
        {
            request_msg_type::bind,
            request_msg_type::describe,
            request_msg_type::execute,
            request_msg_type::sync,
        }
    );
}

void test_execute_typed_optional_args()
{
    statement<std::int32_t, std::string_view> stmt{"myname"};
    request req;
    req.add_execute(stmt, 42, "value", request::param_format::select_best, protocol::format_code::binary, 2);

    // clang-format off
    check_payload(req, {
        // Bind
        0x42, 0x00, 0x00, 0x00, 0x27, 0x00, 0x6d, 0x79, 0x6e, 0x61,
        0x6d, 0x65, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x02, 0x00,
        0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x2a, 0x00, 0x00, 0x00,
        0x05, 0x76, 0x61, 0x6c, 0x75, 0x65, 0x00, 0x01, 0x00, 0x01,

        // Describe
        0x44, 0x00, 0x00, 0x00, 0x06, 0x50, 0x00,

        // Execute
        0x45, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0x00, 0x02,

        // Sync
        0x53, 0x00, 0x00, 0x00, 0x04
    });
    // clang-format on

    check_messages(
        req,
        {
            request_msg_type::bind,
            request_msg_type::describe,
            request_msg_type::execute,
            request_msg_type::sync,
        }
    );
}

// Describe
void test_describe_statement()
{
    request req;
    req.add_describe_statement("myname");

    // clang-format off
    check_payload(req, {
        // Describe
        0x44, 0x00, 0x00, 0x00, 0x0c, 0x53, 0x6d, 0x79, 0x6e, 0x61, 0x6d, 0x65, 0x00,

        // Sync
        0x53, 0x00, 0x00, 0x00, 0x04
    });
    // clang-format on

    check_messages(req, {request_msg_type::describe, request_msg_type::sync});
}

void test_describe_portal()
{
    request req;
    req.add_describe_portal("myname");

    // clang-format off
    check_payload(req, {
        // Describe
        0x44, 0x00, 0x00, 0x00, 0x0c, 0x50, 0x6d, 0x79, 0x6e, 0x61, 0x6d, 0x65, 0x00,

        // Sync
        0x53, 0x00, 0x00, 0x00, 0x04
    });
    // clang-format on

    check_messages(req, {request_msg_type::describe, request_msg_type::sync});
}

// Close
void test_close_statement()
{
    request req;
    req.add_close_statement("myname");

    // clang-format off
    check_payload(req, {
        // Describe
        0x43, 0x00, 0x00, 0x00, 0x0c, 0x53, 0x6d, 0x79, 0x6e, 0x61, 0x6d, 0x65, 0x00,

        // Sync
        0x53, 0x00, 0x00, 0x00, 0x04
    });
    // clang-format on

    check_messages(req, {request_msg_type::close, request_msg_type::sync});
}

void test_close_portal()
{
    request req;
    req.add_close_portal("myname");

    // clang-format off
    check_payload(req, {
        // Describe
        0x43, 0x00, 0x00, 0x00, 0x0c, 0x50, 0x6d, 0x79, 0x6e, 0x61, 0x6d, 0x65, 0x00,

        // Sync
        0x53, 0x00, 0x00, 0x00, 0x04
    });
    // clang-format on

    check_messages(req, {request_msg_type::close, request_msg_type::sync});
}

}  // namespace

int main()
{
    test_simple_query();

    test_query();
    test_query_text();

    test_prepare_untyped();
    test_prepare_typed();

    test_execute_untyped();
    test_execute_typed();
    test_execute_typed_optional_args();

    test_describe_statement();
    test_describe_portal();

    test_close_statement();
    test_close_portal();

    return boost::report_errors();
}