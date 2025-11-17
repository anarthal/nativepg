//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/core/lightweight_test.hpp>
#include <boost/core/span.hpp>
#include <boost/system/error_code.hpp>

#include <array>
#include <cstddef>
#include <string_view>
#include <vector>

#include "../../test_utils/test_utils.hpp"
#include "nativepg/types/pg_bool.hpp"
#include "nativepg/types/pg_float4.hpp"
#include "nativepg/types/pg_float8.hpp"
#include "nativepg/types/pg_int2.hpp"
#include "nativepg/types/pg_int4.hpp"
#include "nativepg/types/pg_text.hpp"
#include "nativepg/types/pg_uuid.hpp"
#include "test_utils.hpp"

using boost::system::error_code;
using nativepg::types::pg_oid_type;
using nativepg::types::pg_type_traits;
using nativepg::types::pg_bool;
using nativepg::types::pg_int2;
using nativepg::types::pg_int4;
using nativepg::types::pg_float4;
using nativepg::types::pg_float8;

using nativepg::types::pg_text;
using nativepg::types::pg_uuid;

// Some test cases have been copied from CPython's and Android's base64 test suites

namespace {

void test_pg_bool()
{
    // Arrange / Setup
    bool test_value = true;

    // Act
    pg_bool subject {test_value};

    // Assert / Checks
    NATIVEPG_TEST_EQ(subject.type_name(), "bool");
    NATIVEPG_TEST_EQ(subject.oid_name(), "BOOL");
    NATIVEPG_TEST_EQ(subject.byte_length(), 1);
    NATIVEPG_TEST_EQ(subject.get(), test_value);
    NATIVEPG_TEST_EQ(subject.supports_binary(), true);
}

void test_pg_int2()
{
    // Arrange / Setup
    std::int16_t test_value = 21;

    // Act
    pg_int2 subject {test_value};

    // Assert / Checks
    NATIVEPG_TEST_EQ(subject.type_name(), "std::int16_t");
    NATIVEPG_TEST_EQ(subject.oid_name(), "INT2");
    NATIVEPG_TEST_EQ(subject.byte_length(), 2);
    NATIVEPG_TEST_EQ(subject.get(), test_value);
    NATIVEPG_TEST_EQ(subject.supports_binary(), true);
}

void test_pg_int4()
{
    // Arrange / Setup
    std::int32_t test_value = 42;

    // Act
    pg_int4 subject {test_value};

    // Assert / Checks
    NATIVEPG_TEST_EQ(subject.type_name(), "std::int32_t");
    NATIVEPG_TEST_EQ(subject.oid_name(), "INT4");
    NATIVEPG_TEST_EQ(subject.byte_length(), 4);
    NATIVEPG_TEST_EQ(subject.get(), test_value);
    NATIVEPG_TEST_EQ(subject.supports_binary(), true);
}

void test_pg_float4()
{
    // Arrange / Setup
    float test_value = 42.7f;

    // Act
    pg_float4 subject {test_value};

    // Assert / Checks
    NATIVEPG_TEST_EQ(subject.type_name(), "float");
    NATIVEPG_TEST_EQ(subject.oid_name(), "FLOAT4");
    NATIVEPG_TEST_EQ(subject.byte_length(), 4);
    NATIVEPG_TEST_EQ(subject.get(), test_value);
    NATIVEPG_TEST_EQ(subject.supports_binary(), true);
}

void test_pg_float8()
{
    // Arrange / Setup
    double test_value = 82.210677;

    // Act
    pg_float8 subject {test_value};

    // Assert / Checks
    NATIVEPG_TEST_EQ(subject.type_name(), "double");
    NATIVEPG_TEST_EQ(subject.oid_name(), "FLOAT8");
    NATIVEPG_TEST_EQ(subject.byte_length(), 8);
    NATIVEPG_TEST_EQ(subject.get(), test_value);
    NATIVEPG_TEST_EQ(subject.supports_binary(), true);
}


void test_pg_text()
{
    // Arrange / Setup
    std::string test_text{"lazy turtle didn't jump over the quick fox!"};

    // Act
    pg_text subject {test_text};

    // Assert / Checks
    NATIVEPG_TEST_EQ(subject.get(), test_text);
    NATIVEPG_TEST_EQ(subject.supports_binary(), true);
    NATIVEPG_TEST_EQ(subject.byte_length(), -1); // variable length
    NATIVEPG_TEST_EQ(subject.type_name(), "std::string");
    NATIVEPG_TEST_EQ(subject.oid_name(), "TEXT");
}

void test_pg_uuid()
{
    // Arrange / Setup
    auto test_value = pg_uuid::from_text("123e4567-e89b-12d3-a456-426655440000");

    // Act
    pg_uuid subject {test_value};

    // Assert / Checks
    NATIVEPG_TEST(subject.encode_text() ==  test_value.encode_text());
    NATIVEPG_TEST_EQ(subject.supports_binary(), true);
    NATIVEPG_TEST_EQ(subject.byte_length(), 16);
    NATIVEPG_TEST_EQ(subject.type_name(), "std::array<std::uint8_t,16>");
    NATIVEPG_TEST_EQ(subject.oid_name(), "UUID");
}

}


int main()
{
    test_pg_bool();

    test_pg_int2();
    test_pg_int4();

    test_pg_float4();
    test_pg_float8();

    test_pg_text();
    test_pg_uuid();

    return boost::report_errors();
}