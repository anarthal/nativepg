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
    using subject_type = nativepg::types::pg_type_traits<bool, pg_oid_type::bool_>;

    bool test_value = true;
    bool result_value = false;
    std::string str;
    boost::system::error_code ec;

    std::vector<std::byte> vec(1, std::byte(0));
    boost::span<std::byte> bytes(vec.data(), vec.size());

    // Act
    ec = subject_type::serialize_binary(test_value, bytes);
    NATIVEPG_TEST_EQ(ec, boost::system::error_code());
    ec = subject_type::parse_binary(bytes, result_value);
    NATIVEPG_TEST_EQ(ec, boost::system::error_code());
    NATIVEPG_TEST_EQ(result_value, test_value);

    ec = subject_type::serialize_text(test_value, str);
    NATIVEPG_TEST_EQ(ec, boost::system::error_code());
    ec = subject_type::parse_text(str, result_value);
    NATIVEPG_TEST_EQ(ec, boost::system::error_code());
    NATIVEPG_TEST_EQ(result_value, test_value);

    // Assert / Checks
    NATIVEPG_TEST_EQ(subject_type::type_name, "bool");
    NATIVEPG_TEST_EQ(subject_type::oid_name, "BOOL");
    NATIVEPG_TEST_EQ(subject_type::byte_len, 1);
    NATIVEPG_TEST_EQ(subject_type::supports_binary, true);
}

void test_pg_int2()
{
    // Arrange / Setup
    using subject_type = nativepg::types::pg_type_traits<std::int16_t, pg_oid_type::int2>;

    std::int16_t test_value = 21;
    std::int16_t result_value;
    std::string str;
    boost::system::error_code ec;

    std::vector<std::byte> vec(2, std::byte(0));
    boost::span<std::byte> bytes(vec.data(), vec.size());

    // Act
    ec = subject_type::serialize_binary(test_value, bytes);
    NATIVEPG_TEST_EQ(ec, boost::system::error_code());
    ec = subject_type::parse_binary(bytes, result_value);
    NATIVEPG_TEST_EQ(ec, boost::system::error_code());
    NATIVEPG_TEST_EQ(result_value, test_value);

    ec = subject_type::serialize_text(test_value, str);
    NATIVEPG_TEST_EQ(ec, boost::system::error_code());
    ec = subject_type::parse_text(str, result_value);
    NATIVEPG_TEST_EQ(ec, boost::system::error_code());
    NATIVEPG_TEST_EQ(result_value, test_value);

    // Assert / Checks
    NATIVEPG_TEST_EQ(subject_type::type_name, "std::int16_t");
    NATIVEPG_TEST_EQ(subject_type::oid_name, "INT2");
    NATIVEPG_TEST_EQ(subject_type::byte_len, 2);
    NATIVEPG_TEST_EQ(subject_type::supports_binary, true);
}

void test_pg_int4()
{
    // Arrange / Setup
    using subject_type = nativepg::types::pg_type_traits<int32_t, pg_oid_type::int4>;
    std::int32_t test_value = 42;
    std::int32_t result_value;
    std::string str;
    boost::system::error_code ec;

    std::vector<std::byte> vec(4, std::byte(0));
    boost::span<std::byte> bytes(vec.data(), vec.size());
    // Act
    ec = subject_type::serialize_binary(test_value, bytes);
    NATIVEPG_TEST_EQ(ec, boost::system::error_code());
    ec = subject_type::parse_binary(bytes, result_value);
    NATIVEPG_TEST_EQ(ec, boost::system::error_code());
    NATIVEPG_TEST_EQ(result_value, test_value);

    ec = subject_type::serialize_text(test_value, str);
    NATIVEPG_TEST_EQ(ec, boost::system::error_code());
    ec = subject_type::parse_text(str, result_value);
    NATIVEPG_TEST_EQ(ec, boost::system::error_code());
    NATIVEPG_TEST_EQ(result_value, test_value);

    // Assert / Checks
    NATIVEPG_TEST_EQ(subject_type::type_name, "std::int32_t");
    NATIVEPG_TEST_EQ(subject_type::oid_name, "INT4");
    NATIVEPG_TEST_EQ(subject_type::byte_len, 4);
    NATIVEPG_TEST_EQ(subject_type::supports_binary, true);
}

void test_pg_float4()
{
    // Arrange / Setup
    using subject_type = nativepg::types::pg_type_traits<float, pg_oid_type::float4>;
    float test_value = 42.7f;
    float result_value;
    std::string str;
    boost::system::error_code ec;
    std::vector<std::byte> vec(4, std::byte(0));
    boost::span<std::byte> bytes(vec.data(), vec.size());

    // Act
    ec = subject_type::serialize_binary(test_value, bytes);
    NATIVEPG_TEST_EQ(ec, boost::system::error_code());
    ec = subject_type::parse_binary(bytes, result_value);
    NATIVEPG_TEST_EQ(ec, boost::system::error_code());
    NATIVEPG_TEST_EQ(result_value, test_value);

    ec = subject_type::serialize_text(test_value, str);
    NATIVEPG_TEST_EQ(ec, boost::system::error_code());
    ec = subject_type::parse_text(str, result_value);
    NATIVEPG_TEST_EQ(ec, boost::system::error_code());
    NATIVEPG_TEST_EQ(result_value, test_value);

    // Assert / Checks
    NATIVEPG_TEST_EQ(subject_type::type_name, "float");
    NATIVEPG_TEST_EQ(subject_type::oid_name, "FLOAT4");
    NATIVEPG_TEST_EQ(subject_type::byte_len, 4);
    NATIVEPG_TEST_EQ(subject_type::supports_binary, true);
}

void test_pg_float8()
{
    // Arrange / Setup
    using subject_type = nativepg::types::pg_type_traits<double, pg_oid_type::float8>;

    double test_value = 82.210677;
    double result_value;
    std::string str;
    boost::system::error_code ec;
    std::vector<std::byte> vec(8, std::byte(0));
    boost::span<std::byte> bytes(vec.data(), vec.size());

    // Act
    ec = subject_type::serialize_binary(test_value, bytes);
    NATIVEPG_TEST_EQ(ec, boost::system::error_code());
    ec = subject_type::parse_binary(bytes, result_value);
    NATIVEPG_TEST_EQ(ec, boost::system::error_code());
    NATIVEPG_TEST_EQ(result_value, test_value);

    ec = subject_type::serialize_text(test_value, str);
    NATIVEPG_TEST_EQ(ec, boost::system::error_code());
    ec = subject_type::parse_text(str, result_value);
    NATIVEPG_TEST_EQ(ec, boost::system::error_code());
    NATIVEPG_TEST_EQ(result_value, test_value);

    // Assert / Checks
    NATIVEPG_TEST_EQ(subject_type::type_name, "double");
    NATIVEPG_TEST_EQ(subject_type::oid_name, "FLOAT8");
    NATIVEPG_TEST_EQ(subject_type::byte_len, 8);
    NATIVEPG_TEST_EQ(subject_type::supports_binary, true);
}


void test_pg_text()
{
    // Arrange / Setup
    using subject_type = nativepg::types::pg_type_traits<std::string, pg_oid_type::text>;

    std::string test_value{"lazy turtle didn't jump over the quick fox!"};
    std::string result_value;
    std::string str;
    boost::system::error_code ec;
    std::vector<std::byte> vec(test_value.size(), std::byte(0));
    boost::span<std::byte> bytes(vec.data(), vec.size());

    // Act
    ec = subject_type::serialize_binary(test_value, bytes);
    NATIVEPG_TEST_EQ(ec, boost::system::error_code());
    ec = subject_type::parse_binary(bytes, result_value);
    NATIVEPG_TEST_EQ(ec, boost::system::error_code());
    NATIVEPG_TEST_EQ(result_value, test_value);

    ec = subject_type::serialize_text(test_value, str);
    NATIVEPG_TEST_EQ(ec, boost::system::error_code());
    ec = subject_type::parse_text(str, result_value);
    NATIVEPG_TEST_EQ(ec, boost::system::error_code());
    NATIVEPG_TEST_EQ(result_value, test_value);

    // Assert / Checks
    NATIVEPG_TEST_EQ(subject_type::supports_binary, true);
    NATIVEPG_TEST_EQ(subject_type::byte_len, -1); // variable length
    NATIVEPG_TEST_EQ(subject_type::type_name, "std::string");
    NATIVEPG_TEST_EQ(subject_type::oid_name, "TEXT");
}

void test_pg_uuid()
{
    // Arrange / Setup
    using subject_type = nativepg::types::pg_type_traits<boost::uuids::uuid, pg_oid_type::uuid>;

    auto const test_value = nativepg::types::to_uuid("123e4567-e89b-12d3-a456-426655440000");
    boost::uuids::uuid result_value;
    std::string str;
    boost::system::error_code ec;

    std::vector<std::byte> vec(16, std::byte(0));
    boost::span<std::byte> bytes(vec.data(), vec.size());

    // Act & Check
    ec = subject_type::serialize_binary(test_value, bytes);
    NATIVEPG_TEST_EQ(ec, boost::system::error_code());
    ec = subject_type::parse_binary(bytes, result_value);
    NATIVEPG_TEST_EQ(ec, boost::system::error_code());
    NATIVEPG_TEST_EQ(result_value, test_value);

    ec = subject_type::serialize_text(test_value, str);
    NATIVEPG_TEST_EQ(ec, boost::system::error_code());
    ec = subject_type::parse_text(str, result_value);
    NATIVEPG_TEST_EQ(ec, boost::system::error_code());
    NATIVEPG_TEST_EQ(result_value, test_value);

    // Assert / Checks
    NATIVEPG_TEST_EQ(subject_type::supports_binary, true);
    NATIVEPG_TEST_EQ(subject_type::byte_len, 16);
    NATIVEPG_TEST_EQ(subject_type::type_name, "boost::uuids::uuid");
    NATIVEPG_TEST_EQ(subject_type::oid_name, "UUID");
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