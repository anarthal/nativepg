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

// Some test cases have been copied from CPython's and Android's base64 test suites

namespace {

void test_pg_bool()
{
    // Arrange / Setup
    bool test_value = true;
    bool result_value = false;
    std::string str;
    boost::system::error_code ec;
    boost::span<const std::byte> bytes_in;
    boost::span<std::byte> bytes_out;

    // Act
    nativepg::types::pg_bool_traits subject;

    ec = subject.serialize_binary(test_value, bytes_out);
    ec = subject.parse_binary(bytes_in, result_value);

    ec = subject.serialize_text(test_value, str);
    ec = subject.parse_text(str, result_value);

    // Assert / Checks
    NATIVEPG_TEST_EQ(subject.type_name, "bool");
    NATIVEPG_TEST_EQ(subject.oid_name, "BOOL");
    NATIVEPG_TEST_EQ(subject.byte_len, 1);
    NATIVEPG_TEST_EQ(subject.supports_binary, true);
}

void test_pg_int2()
{
    // Arrange / Setup
    std::int16_t test_value = 21;
    std::int16_t result_value;
    std::string str;
    boost::system::error_code ec;
    boost::span<const std::byte> bytes_in;
    boost::span<std::byte> bytes_out;

    // Act
    nativepg::types::pg_int2_traits subject;
    ec = subject.serialize_binary(test_value, bytes_out);
    ec = subject.parse_binary(bytes_in, result_value);

    ec = subject.serialize_text(test_value, str);
    ec = subject.parse_text(str, result_value);

    // Assert / Checks
    NATIVEPG_TEST_EQ(subject.type_name, "std::int16_t");
    NATIVEPG_TEST_EQ(subject.oid_name, "INT2");
    NATIVEPG_TEST_EQ(subject.byte_len, 2);
    NATIVEPG_TEST_EQ(subject.supports_binary, true);
}

void test_pg_int4()
{
    // Arrange / Setup
    std::int32_t test_value = 42;
    std::int32_t result_value;
    std::string str;
    boost::system::error_code ec;
    boost::span<const std::byte> bytes_in;
    boost::span<std::byte> bytes_out;

    // Act
    nativepg::types::pg_int4_traits subject;
    ec = subject.serialize_binary(test_value, bytes_out);
    ec = subject.parse_binary(bytes_in, result_value);

    ec = subject.serialize_text(test_value, str);
    ec = subject.parse_text(str, result_value);

    // Assert / Checks
    NATIVEPG_TEST_EQ(subject.type_name, "std::int32_t");
    NATIVEPG_TEST_EQ(subject.oid_name, "INT4");
    NATIVEPG_TEST_EQ(subject.byte_len, 4);
    NATIVEPG_TEST_EQ(subject.supports_binary, true);
}

void test_pg_float4()
{
    // Arrange / Setup
    float test_value = 42.7f;
    float result_value;
    std::string str;
    boost::system::error_code ec;
    boost::span<const std::byte> bytes_in;
    boost::span<std::byte> bytes_out;

    // Act
    nativepg::types::pg_float4_traits subject;
    ec = subject.serialize_binary(test_value, bytes_out);
    ec = subject.parse_binary(bytes_in, result_value);

    ec = subject.serialize_text(test_value, str);
    ec = subject.parse_text(str, result_value);

    // Assert / Checks
    NATIVEPG_TEST_EQ(subject.type_name, "float");
    NATIVEPG_TEST_EQ(subject.oid_name, "FLOAT4");
    NATIVEPG_TEST_EQ(subject.byte_len, 4);
    NATIVEPG_TEST_EQ(subject.supports_binary, true);
}

void test_pg_float8()
{
    // Arrange / Setup
    double test_value = 82.210677;
    double result_value;
    std::string str;
    boost::system::error_code ec;
    boost::span<const std::byte> bytes_in;
    boost::span<std::byte> bytes_out;

    // Act
    nativepg::types::pg_float8_traits subject;
    ec = subject.serialize_binary(test_value, bytes_out);
    ec = subject.parse_binary(bytes_in, result_value);

    ec = subject.serialize_text(test_value, str);
    ec = subject.parse_text(str, result_value);

    // Assert / Checks
    NATIVEPG_TEST_EQ(subject.type_name, "double");
    NATIVEPG_TEST_EQ(subject.oid_name, "FLOAT8");
    NATIVEPG_TEST_EQ(subject.byte_len, 8);
    NATIVEPG_TEST_EQ(subject.supports_binary, true);
}


void test_pg_text()
{
    // Arrange / Setup
    std::string test_value{"lazy turtle didn't jump over the quick fox!"};
    std::string result_value;
    std::string str;
    boost::system::error_code ec;
    boost::span<const std::byte> bytes_in = {};
    boost::span<std::byte> bytes_out = {};

    // Act
    nativepg::types::pg_text_traits subject;
    ec = subject.serialize_binary(test_value, bytes_out);
    ec = subject.parse_binary(bytes_in, result_value);

    ec = subject.serialize_text(test_value, str);
    ec = subject.parse_text(str, result_value);

    // Assert / Checks
    NATIVEPG_TEST_EQ(subject.supports_binary, true);
    NATIVEPG_TEST_EQ(subject.byte_len, -1); // variable length
    NATIVEPG_TEST_EQ(subject.type_name, "std::string");
    NATIVEPG_TEST_EQ(subject.oid_name, "TEXT");
}

void test_pg_uuid()
{
    // Arrange / Setup
    auto const test_value = nativepg::types::to_uuid("123e4567-e89b-12d3-a456-426655440000");
    boost::uuids::uuid result_value;
    std::string str;
    boost::system::error_code ec;
    boost::span<const std::byte> bytes_in = {};
    boost::span<std::byte> bytes_out = {};

    // Act
    nativepg::types::pg_uuid_traits subject;
    ec = subject.serialize_binary(test_value, bytes_out);
    ec = subject.parse_binary(bytes_in, result_value);

    ec = subject.serialize_text(test_value, str);
    ec = subject.parse_text(str, result_value);

    // Assert / Checks
    NATIVEPG_TEST_EQ(subject.supports_binary, true);
    NATIVEPG_TEST_EQ(subject.byte_len, 16);
    NATIVEPG_TEST_EQ(subject.type_name, "boost::uuids::uuid");
    NATIVEPG_TEST_EQ(subject.oid_name, "UUID");
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