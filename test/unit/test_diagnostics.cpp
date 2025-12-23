//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/core/lightweight_test.hpp>

#include "nativepg/extended_error.hpp"
#include "nativepg/protocol/notice_error.hpp"

using namespace nativepg;

namespace {

// Simple case, where the basic fields are present
// This is the case when an error to a query is received
void test_format_simple()
{
    protocol::error_response msg{
        {.severity = "ERROR",
         .localized_severity = "ERROR_LOC",
         .sqlstate = "42P01",
         .message = "relation does not exist",
         .position = "15",
         .file_name = "parse_relation.c",
         .line_number = "1449",
         .routine = "parserOpenTable"}
    };
    diagnostics diag{msg};

    BOOST_TEST_EQ(diag.message(), "ERROR: 42P01: relation does not exist");
}

// We don't segfault if any fields are missing
void test_format_missing_fields()
{
    diagnostics diag{protocol::error_response{}};

    BOOST_TEST_EQ(
        diag.message(),
        "<Server error with unknown severity>: <unknown SQLSTATE>: <unknown error>"
    );
}

// TODO: rest of the formatting cases

// Other member functions
void test_default_ctor()
{
    diagnostics diag;
    BOOST_TEST_EQ(diag.message(), "");
}

void test_assign()
{
    diagnostics diag{protocol::error_response{}};
    protocol::error_response msg{
        {.severity = "ERROR",
         .localized_severity = "ERROR_LOC",
         .sqlstate = "42P01",
         .message = "relation does not exist",
         .position = "15",
         .file_name = "parse_relation.c",
         .line_number = "1449",
         .routine = "parserOpenTable"}
    };
    diag.assign(msg);

    BOOST_TEST_EQ(diag.message(), "ERROR: 42P01: relation does not exist");
}

}  // namespace

int main()
{
    test_format_simple();
    test_format_missing_fields();

    test_default_ctor();
    test_assign();

    return boost::report_errors();
}