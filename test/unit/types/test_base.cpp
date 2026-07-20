//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/core/lightweight_test.hpp>
#include <boost/system/error_code.hpp>

#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <span>
#include <sstream>
#include <string>
#include <vector>

#include "nativepg/detail/field_traits.hpp"
#include "nativepg/protocol/describe.hpp"
#include "nativepg/types/base.hpp"
#include "test_utils/test_utils.hpp"

using namespace nativepg;
using boost::system::error_code;

namespace {

std::ostream& operator<<(std::ostream& os, const std::vector<std::byte>& bytes)
{
    if (bytes.empty())
    {
        os << "0x00";
    }
    else
    {
        os << "0x";
        for (auto byte : bytes)
            os << std::format("{:02x}", static_cast<int>(byte));
    }

    return os;
}

// Builds a field_description with the given type OID and format code (the rest of the fields are
// irrelevant to type parsing)
protocol::field_description make_field_description(
    std::int32_t type_oid,
    protocol::format_code fmt_code = protocol::format_code::text
)
{
    return {
        .name = "field",
        .table_oid = 0,
        .column_attribute = 0,
        .type_oid = type_oid,
        .type_length = 0,
        .type_modifier = 0,
        .fmt_code = fmt_code,
    };
}

// BOOL
void test_parse_text_bool_t_success()
{
    // Arrange
    bool b = false;
    std::string str = "t";
    boost::span<const unsigned char> data(reinterpret_cast<const unsigned char*>(str.data()), str.size());
    field_view fv{data};

    // Act
    auto err = types::parse_text_bool(fv, b);

    // Assert
    NATIVEPG_TEST_EQ(err, boost::system::errc::success);
    NATIVEPG_TEST_EQ(b, true);
}

void test_parse_binary_bool_t_success()
{
    // Arrange
    bool b = false;
    static constexpr unsigned char pg_bool[] = {0x1};
    boost::span<const unsigned char> data(pg_bool);
    field_view fv{data};

    // Act
    auto err = types::parse_binary_bool(fv, b);

    // Assert
    NATIVEPG_TEST_EQ(err, boost::system::errc::success);
    NATIVEPG_TEST_EQ(b, true);
}

void test_parse_text_bool_f_success()
{
    // Arrange
    bool b = true;
    std::string str = "f";
    boost::span<const unsigned char> data(reinterpret_cast<const unsigned char*>(str.data()), str.size());
    field_view fv{data};

    // Act
    auto err = types::parse_text_bool(fv, b);

    // Assert
    NATIVEPG_TEST_EQ(err, boost::system::errc::success);
    NATIVEPG_TEST_EQ(b, false);
}

void test_parse_binary_bool_f_success()
{
    // Arrange
    bool b = true;
    static constexpr unsigned char pg_bool[] = {0x0};
    boost::span<const unsigned char> data(pg_bool);
    field_view fv{data};

    // Act
    auto err = types::parse_binary_bool(fv, b);

    // Assert
    NATIVEPG_TEST_EQ(err, boost::system::errc::success);
    NATIVEPG_TEST_EQ(b, false);
}

void test_parse_text_bool_invalid_error()
{
    // Arrange
    bool b = false;
    std::string str = "true";  // Only "t" and "f" are valid
    boost::span<const unsigned char> data(reinterpret_cast<const unsigned char*>(str.data()), str.size());
    field_view fv{data};

    // Act
    auto err = types::parse_text_bool(fv, b);

    // Assert
    NATIVEPG_TEST_EQ(err, error_code(client_errc::protocol_value_error));
}

void test_parse_binary_bool_invalid_size_error()
{
    // Arrange
    bool b = false;
    static constexpr unsigned char pg_bool[] = {0x0, 0x1};  // Only 1 byte is valid
    boost::span<const unsigned char> data(pg_bool);
    field_view fv{data};

    // Act
    auto err = types::parse_binary_bool(fv, b);

    // Assert
    NATIVEPG_TEST_EQ(err, error_code(client_errc::protocol_value_error));
}

// BYTEA
void test_parse_text_bytea_success()
{
    // Arrange
    std::vector<std::byte> ba;
    std::string str = "\\x21061977";
    boost::span<const unsigned char> data(reinterpret_cast<const unsigned char*>(str.data()), str.size());
    field_view fv{data};

    // Act
    auto err = types::parse_text_bytea(fv, ba);

    // Assert
    NATIVEPG_TEST_EQ(err, boost::system::errc::success);
    NATIVEPG_TEST_EQ(ba.size(), 4u);
    std::stringstream ss;
    ss << ba;
    NATIVEPG_TEST_EQ(ss.str(), "0x21061977");
}

void test_parse_binary_bytea_success()
{
    // Arrange
    std::vector<std::byte> ba;
    static constexpr unsigned char pg_bytea[] = {0x21, 0x06, 0x19, 0x77};
    boost::span<const unsigned char> data(pg_bytea);
    field_view fv{data};

    // Act
    auto err = types::parse_binary_bytea(fv, ba);

    // Assert
    NATIVEPG_TEST_EQ(err, boost::system::errc::success);
    NATIVEPG_TEST_EQ(ba.size(), 4u);
    std::stringstream ss;
    ss << ba;
    NATIVEPG_TEST_EQ(ss.str(), "0x21061977");
}

void test_parse_text_bytea_missing_prefix_error()
{
    // Arrange
    std::vector<std::byte> ba;
    std::string str = "21061977";  // Missing the \x prefix
    boost::span<const unsigned char> data(reinterpret_cast<const unsigned char*>(str.data()), str.size());
    field_view fv{data};

    // Act
    auto err = types::parse_text_bytea(fv, ba);

    // Assert
    NATIVEPG_TEST_EQ(err, error_code(client_errc::protocol_value_error));
}

void test_parse_text_bytea_odd_length_error()
{
    // Arrange
    std::vector<std::byte> ba;
    std::string str = "\\x210";  // Odd number of hex digits
    boost::span<const unsigned char> data(reinterpret_cast<const unsigned char*>(str.data()), str.size());
    field_view fv{data};

    // Act
    auto err = types::parse_text_bytea(fv, ba);

    // Assert
    NATIVEPG_TEST_EQ(err, error_code(client_errc::protocol_value_error));
}

void test_parse_text_bytea_invalid_hex_error()
{
    // Arrange
    std::vector<std::byte> ba;
    std::string str = "\\xzz";  // Not valid hex digits
    boost::span<const unsigned char> data(reinterpret_cast<const unsigned char*>(str.data()), str.size());
    field_view fv{data};

    // Act
    auto err = types::parse_text_bytea(fv, ba);

    // Assert
    NATIVEPG_TEST_EQ(err, error_code(client_errc::protocol_value_error));
}

// INT
template <typename T, T in_val>
void test_parse_text_int_success()
{
    // Arrange
    T out_val;
    std::string str = std::to_string(in_val);
    boost::span<const unsigned char> data(reinterpret_cast<const unsigned char*>(str.data()), str.size());
    field_view fv{data};

    // Act
    auto err = types::parse_text_int<T>(fv, out_val);

    // Assert
    NATIVEPG_TEST_EQ(err, boost::system::errc::success);
    NATIVEPG_TEST_EQ(out_val, in_val);
}

template <typename T, T in_val>
void test_parse_binary_int_success()
{
    // Arrange
    T out_val;
    std::vector<uint8_t> buffer(sizeof(T));
    boost::endian::endian_store<T, sizeof(T), boost::endian::order::big>(buffer.data(), in_val);
    field_view fv{buffer};

    // Act
    auto err = types::parse_binary_int<T>(fv, out_val);

    // Assert
    NATIVEPG_TEST_EQ(err, boost::system::errc::success);
    NATIVEPG_TEST_EQ(out_val, in_val);
}

template <typename T>
void test_parse_text_int_garbage_error()
{
    // Arrange
    T out_val{};
    std::string str = "not_a_number";
    boost::span<const unsigned char> data(reinterpret_cast<const unsigned char*>(str.data()), str.size());
    field_view fv{data};

    // Act
    auto err = types::parse_text_int<T>(fv, out_val);

    // Assert
    NATIVEPG_TEST_EQ(err, error_code(client_errc::protocol_value_error));
}

template <typename T>
void test_parse_text_int_overflow_error()
{
    // Arrange
    T out_val{};
    // One more than the maximum representable value for T
    std::string str = std::to_string(static_cast<std::intmax_t>(std::numeric_limits<T>::max()) + 1);
    boost::span<const unsigned char> data(reinterpret_cast<const unsigned char*>(str.data()), str.size());
    field_view fv{data};

    // Act
    auto err = types::parse_text_int<T>(fv, out_val);

    // Assert
    NATIVEPG_TEST_EQ(err, error_code(client_errc::protocol_value_error));
}

template <typename T>
void test_parse_binary_int_wrong_size_error()
{
    // Arrange
    T out_val{};
    std::vector<uint8_t> buffer(sizeof(T) + 1, 0);  // Wrong size
    field_view fv{buffer};

    // Act
    auto err = types::parse_binary_int<T>(fv, out_val);

    // Assert
    NATIVEPG_TEST_EQ(err, error_code(client_errc::protocol_value_error));
}

// FLOAT
template <typename T, T in_val>
void test_parse_text_float_success()
{
    // Arrange
    T out_val;
    std::stringstream ss;
    ss << std::setprecision(std::numeric_limits<T>::max_digits10) << in_val;
    std::string str = ss.str();
    boost::span<const unsigned char> data(reinterpret_cast<const unsigned char*>(str.data()), str.size());
    field_view fv{data};

    // Act
    auto err = types::parse_text_float<T>(fv, out_val);

    // Assert
    NATIVEPG_TEST_EQ(err, boost::system::errc::success);
    if (std::isnan(in_val))
    {
        NATIVEPG_TEST(std::isnan(out_val));
    }
    else if (std::isinf(in_val))
    {
        NATIVEPG_TEST(std::isinf(out_val));
        NATIVEPG_TEST_EQ(std::signbit(out_val), std::signbit(in_val));
    }
    else
    {
        NATIVEPG_TEST_EQ(out_val, in_val);
    }
}

template <typename T, T in_val>
void test_parse_binary_float_success()
{
    // Arrange
    T out_val;
    std::vector<uint8_t> buffer(sizeof(T));
    boost::endian::endian_store<T, sizeof(T), boost::endian::order::big>(buffer.data(), in_val);
    field_view fv{buffer};

    // Act
    auto err = types::parse_binary_float<T>(fv, out_val);

    // Assert
    NATIVEPG_TEST_EQ(err, boost::system::errc::success);
    if (std::isnan(in_val))
    {
        NATIVEPG_TEST(std::isnan(out_val));
    }
    else if (std::isinf(in_val))
    {
        NATIVEPG_TEST(std::isinf(out_val));
        NATIVEPG_TEST_EQ(std::signbit(out_val), std::signbit(in_val));
    }
    else
    {
        NATIVEPG_TEST_EQ(out_val, in_val);
    }
}

template <std::size_t N>
void test_parse_binary_float_success(const unsigned char (&in_val)[N])
{
    static_assert(N == sizeof(float) || N == sizeof(double), "Unsupported byte width for a PostgreSQL float");
    using T = std::conditional_t<N == sizeof(float), float, double>;

    // Arrange
    T out_val{};
    boost::span<const unsigned char> data(in_val);
    field_view fv{data};

    // Act
    auto err = types::parse_binary_float<T>(fv, out_val);

    // Assert
    NATIVEPG_TEST_EQ(err, boost::system::errc::success);

    const T expected = boost::endian::endian_load<T, sizeof(T), boost::endian::order::big>(in_val);
    if (std::isnan(expected))
    {
        NATIVEPG_TEST(std::isnan(out_val));
    }
    else if (std::isinf(expected))
    {
        NATIVEPG_TEST(std::isinf(out_val));
        NATIVEPG_TEST_EQ(std::signbit(out_val), std::signbit(expected));
    }
    else
    {
        NATIVEPG_TEST_EQ(out_val, expected);
    }
}

template <typename T>
void test_parse_text_float_garbage_error()
{
    // Arrange
    T out_val{};
    std::string str = "not_a_float";
    boost::span<const unsigned char> data(reinterpret_cast<const unsigned char*>(str.data()), str.size());
    field_view fv{data};

    // Act
    auto err = types::parse_text_float<T>(fv, out_val);

    // Assert
    NATIVEPG_TEST_EQ(err, error_code(client_errc::protocol_value_error));
}

template <typename T>
void test_parse_binary_float_wrong_size_error()
{
    // Arrange
    T out_val{};
    std::vector<uint8_t> buffer(sizeof(T) + 1, 0);  // Wrong size
    field_view fv{buffer};

    // Act
    auto err = types::parse_binary_float<T>(fv, out_val);

    // Assert
    NATIVEPG_TEST_EQ(err, error_code(client_errc::protocol_value_error));
}

template <typename T = std::string>
void test_parse_text_text_success(const T& in_val)
{
    // Arrange
    T out_val;
    const std::string str = in_val;
    boost::span<const unsigned char> data(reinterpret_cast<const unsigned char*>(str.data()), str.size());
    field_view fv{data};

    // Act
    auto err = types::parse_text_text<T>(fv, out_val);

    // Assert
    NATIVEPG_TEST_EQ(err, boost::system::errc::success);
    NATIVEPG_TEST_EQ(out_val, in_val);
}

template <typename T = std::string>
void test_parse_binary_text_success(const T& in_val)
{
    // Arrange
    T out_val;
    const std::string_view str = in_val;
    boost::span<const unsigned char> data(reinterpret_cast<const unsigned char*>(str.data()), str.size());
    field_view fv{data};

    // Act
    auto err = types::parse_binary_text<T>(fv, out_val);

    // Assert
    NATIVEPG_TEST_EQ(err, boost::system::errc::success);
    NATIVEPG_TEST_EQ(out_val, in_val);

    if constexpr (std::is_same_v<T, std::string_view>)
    {
        NATIVEPG_TEST_EQ(
            static_cast<const void*>(out_val.data()),
            static_cast<const void*>(data.data())
        );  // zero-copy: aliases input
    }
    else
    {
        NATIVEPG_TEST(
            static_cast<const void*>(out_val.data()) != static_cast<const void*>(data.data())
        );  // std::string: owns a copy
    }
}

//
// detail::field_is_compatible / detail::field_parse (field_traits_base.hpp)
//
void test_field_is_compatible_bool_success()
{
    NATIVEPG_TEST_EQ(
        detail::field_is_compatible<bool>::call(make_field_description(detail::bool_oid)),
        boost::system::errc::success
    );
}

void test_field_is_compatible_bool_incompatible_error()
{
    NATIVEPG_TEST_EQ(
        detail::field_is_compatible<bool>::call(make_field_description(detail::int4_oid)),
        error_code(client_errc::incompatible_field_type)
    );
}

void test_field_is_compatible_int_widening_success()
{
    // A smaller wire type is compatible with a wider C++ type
    NATIVEPG_TEST_EQ(
        detail::field_is_compatible<std::int32_t>::call(make_field_description(detail::int2_oid)),
        boost::system::errc::success
    );
    NATIVEPG_TEST_EQ(
        detail::field_is_compatible<std::int64_t>::call(make_field_description(detail::int4_oid)),
        boost::system::errc::success
    );
}

void test_field_is_compatible_int_narrowing_error()
{
    // A wider wire type is not compatible with a narrower C++ type
    NATIVEPG_TEST_EQ(
        detail::field_is_compatible<std::int16_t>::call(make_field_description(detail::int4_oid)),
        error_code(client_errc::incompatible_field_type)
    );
    NATIVEPG_TEST_EQ(
        detail::field_is_compatible<std::int32_t>::call(make_field_description(detail::int8_oid)),
        error_code(client_errc::incompatible_field_type)
    );
}

void test_field_is_compatible_string_success()
{
    NATIVEPG_TEST_EQ(
        detail::field_is_compatible<std::string>::call(make_field_description(detail::text_oid)),
        boost::system::errc::success
    );
    NATIVEPG_TEST_EQ(
        detail::field_is_compatible<std::string>::call(make_field_description(detail::varchar_oid)),
        boost::system::errc::success
    );
}

void test_field_parse_unexpected_null_error()
{
    // Arrange
    bool b = false;
    field_view fv;  // NULL
    const auto desc = make_field_description(detail::bool_oid);

    // Act
    auto err = detail::field_parse<bool>::call(fv, desc, b);

    // Assert
    NATIVEPG_TEST_EQ(err, error_code(client_errc::unexpected_null));
}

void test_field_parse_bool_text_success()
{
    // Arrange
    bool b = false;
    std::string str = "t";
    boost::span<const unsigned char> data(reinterpret_cast<const unsigned char*>(str.data()), str.size());
    field_view fv{data};
    const auto desc = make_field_description(detail::bool_oid, protocol::format_code::text);

    // Act
    auto err = detail::field_parse<bool>::call(fv, desc, b);

    // Assert
    NATIVEPG_TEST_EQ(err, boost::system::errc::success);
    NATIVEPG_TEST_EQ(b, true);
}

void test_field_parse_int32_from_int2_wire_success()
{
    // Arrange
    std::int32_t out_val = 0;
    static constexpr unsigned char pg_int2[] = {0x00, 0x2a};  // 42, big endian
    boost::span<const unsigned char> data(pg_int2);
    field_view fv{data};
    const auto desc = make_field_description(detail::int2_oid, protocol::format_code::binary);

    // Act
    auto err = detail::field_parse<std::int32_t>::call(fv, desc, out_val);

    // Assert
    NATIVEPG_TEST_EQ(err, boost::system::errc::success);
    NATIVEPG_TEST_EQ(out_val, 42);
}

}  // namespace

int main()
{
    // BOOL
    test_parse_text_bool_t_success();
    test_parse_binary_bool_t_success();
    test_parse_text_bool_f_success();
    test_parse_binary_bool_f_success();
    test_parse_text_bool_invalid_error();
    test_parse_binary_bool_invalid_size_error();

    // BYTEA
    test_parse_text_bytea_success();
    test_parse_binary_bytea_success();
    test_parse_text_bytea_missing_prefix_error();
    test_parse_text_bytea_odd_length_error();
    test_parse_text_bytea_invalid_hex_error();

    // INT2
    test_parse_text_int_success<std::int16_t, std::numeric_limits<std::int16_t>::min()>();
    test_parse_text_int_success<std::int16_t, std::numeric_limits<std::int16_t>::max()>();
    test_parse_binary_int_success<std::int16_t, std::numeric_limits<std::int16_t>::min()>();
    test_parse_binary_int_success<std::int16_t, std::numeric_limits<std::int16_t>::max()>();
    // INT4
    test_parse_text_int_success<std::int32_t, std::numeric_limits<std::int32_t>::min()>();
    test_parse_text_int_success<std::int32_t, std::numeric_limits<std::int32_t>::max()>();
    test_parse_binary_int_success<std::int32_t, std::numeric_limits<std::int32_t>::min()>();
    test_parse_binary_int_success<std::int32_t, std::numeric_limits<std::int32_t>::max()>();
    // INT8
    test_parse_text_int_success<std::int64_t, std::numeric_limits<std::int64_t>::min()>();
    test_parse_text_int_success<std::int64_t, std::numeric_limits<std::int64_t>::max()>();
    test_parse_binary_int_success<std::int64_t, std::numeric_limits<std::int64_t>::min()>();
    test_parse_binary_int_success<std::int64_t, std::numeric_limits<std::int64_t>::max()>();
    // Invalid INT paths
    test_parse_text_int_garbage_error<std::int16_t>();
    test_parse_text_int_garbage_error<std::int32_t>();
    test_parse_text_int_garbage_error<std::int64_t>();
    test_parse_text_int_overflow_error<std::int16_t>();
    test_parse_text_int_overflow_error<std::int32_t>();
    test_parse_binary_int_wrong_size_error<std::int16_t>();
    test_parse_binary_int_wrong_size_error<std::int32_t>();
    test_parse_binary_int_wrong_size_error<std::int64_t>();

    // FLOAT4
    test_parse_text_float_success<float, std::numeric_limits<float>::min()>();
    test_parse_text_float_success<float, std::numeric_limits<float>::max()>();
    test_parse_text_float_success<float, std::numeric_limits<float>::quiet_NaN()>();
    test_parse_text_float_success<float, std::numeric_limits<float>::infinity()>();
    test_parse_text_float_success<float, -std::numeric_limits<float>::infinity()>();
    test_parse_binary_float_success<float, std::numeric_limits<float>::min()>();
    test_parse_binary_float_success<float, std::numeric_limits<float>::max()>();
    test_parse_binary_float_success<float, std::numeric_limits<float>::quiet_NaN()>();
    test_parse_binary_float_success<float, std::numeric_limits<float>::infinity()>();
    test_parse_binary_float_success<float, -std::numeric_limits<float>::infinity()>();

    // FLOAT8
    test_parse_text_float_success<double, std::numeric_limits<double>::min()>();
    test_parse_text_float_success<double, std::numeric_limits<double>::max()>();
    test_parse_text_float_success<double, std::numeric_limits<double>::quiet_NaN()>();
    test_parse_text_float_success<double, std::numeric_limits<double>::infinity()>();
    test_parse_text_float_success<double, -std::numeric_limits<double>::infinity()>();
    test_parse_binary_float_success<double, std::numeric_limits<double>::min()>();
    test_parse_binary_float_success<double, std::numeric_limits<double>::max()>();
    test_parse_binary_float_success<double, std::numeric_limits<double>::quiet_NaN()>();
    test_parse_binary_float_success<double, std::numeric_limits<double>::infinity()>();
    test_parse_binary_float_success<double, -std::numeric_limits<double>::infinity()>();

    // float4
    static constexpr unsigned char pg_float4_inf[] = {0x7F, 0x80, 0x00, 0x00};
    static constexpr unsigned char pg_float4_ninf[] = {0xFF, 0x80, 0x00, 0x00};
    static constexpr unsigned char pg_float4_nan[] = {0x7F, 0xC0, 0x00, 0x00};
    test_parse_binary_float_success(pg_float4_inf);
    test_parse_binary_float_success(pg_float4_ninf);
    test_parse_binary_float_success(pg_float4_nan);

    // float8
    static constexpr unsigned char pg_float8_inf[] = {0x7F, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    static constexpr unsigned char pg_float8_ninf[] = {0xFF, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    static constexpr unsigned char pg_float8_nan[] = {0x7F, 0xF8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    test_parse_binary_float_success(pg_float8_inf);
    test_parse_binary_float_success(pg_float8_ninf);
    test_parse_binary_float_success(pg_float8_nan);

    // Invalid FLOAT paths
    test_parse_text_float_garbage_error<float>();
    test_parse_text_float_garbage_error<double>();
    test_parse_binary_float_wrong_size_error<float>();
    test_parse_binary_float_wrong_size_error<double>();

    // TEXT / VARCHAR
    test_parse_text_text_success<std::string>("The quick brown fox jumps over the lazy dog!");
    test_parse_binary_text_success<std::string>("The quick brown fox jumps over the lazy dog!");

    // detail::field_is_compatible / detail::field_parse
    test_field_is_compatible_bool_success();
    test_field_is_compatible_bool_incompatible_error();
    test_field_is_compatible_int_widening_success();
    test_field_is_compatible_int_narrowing_error();
    test_field_is_compatible_string_success();
    test_field_parse_unexpected_null_error();
    test_field_parse_bool_text_success();
    test_field_parse_int32_from_int2_wire_success();

    return boost::report_errors();
}
