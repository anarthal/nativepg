//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/core/lightweight_test.hpp>
#include <boost/endian/conversion.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <iomanip>
#include <limits>
#include <optional>
#include <source_location>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "nativepg/client_errc.hpp"
#include "nativepg/field_traits.hpp"
#include "nativepg/protocol/common.hpp"
#include "nativepg/types/base.hpp"
#include "test_utils/test_range_eq.hpp"

using namespace nativepg;
using std::error_code;

namespace {

std::ostream& operator<<(std::ostream& os, const std::span<std::byte>& bytes)
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

// BOOL
void test_parse_text_bool_t_success()
{
    // Arrange
    bool b = false;
    std::string str = "t";
    std::span<const unsigned char> data(reinterpret_cast<const unsigned char*>(str.data()), str.size());
    field_view fv{data};

    // Act
    auto err = types::parse_text_bool(fv, b);

    // Assert
    BOOST_TEST_EQ(err, std::error_code());
    BOOST_TEST_EQ(b, true);
}

void test_parse_binary_bool_t_success()
{
    // Arrange
    bool b = false;
    static constexpr unsigned char pg_bool[] = {0x1};
    std::span<const unsigned char> data(pg_bool);
    field_view fv{data};

    // Act
    auto err = types::parse_binary_bool(fv, b);

    // Assert
    BOOST_TEST_EQ(err, std::error_code());
    BOOST_TEST_EQ(b, true);
}

void test_parse_text_bool_f_success()
{
    // Arrange
    bool b = true;
    std::string str = "f";
    std::span<const unsigned char> data(reinterpret_cast<const unsigned char*>(str.data()), str.size());
    field_view fv{data};

    // Act
    auto err = types::parse_text_bool(fv, b);

    // Assert
    BOOST_TEST_EQ(err, std::error_code());
    BOOST_TEST_EQ(b, false);
}

void test_parse_binary_bool_f_success()
{
    // Arrange
    bool b = true;
    static constexpr unsigned char pg_bool[] = {0x0};
    std::span<const unsigned char> data(pg_bool);
    field_view fv{data};

    // Act
    auto err = types::parse_binary_bool(fv, b);

    // Assert
    BOOST_TEST_EQ(err, std::error_code());
    BOOST_TEST_EQ(b, false);
}

void test_parse_text_bool_invalid_error()
{
    // Arrange
    bool b = false;
    std::string str = "true";  // Only "t" and "f" are valid
    std::span<const unsigned char> data(reinterpret_cast<const unsigned char*>(str.data()), str.size());
    field_view fv{data};

    // Act
    auto err = types::parse_text_bool(fv, b);

    // Assert
    BOOST_TEST_EQ(err, error_code(client_errc::protocol_value_error));
}

void test_parse_binary_bool_invalid_size_error()
{
    // Arrange
    bool b = false;
    static constexpr unsigned char pg_bool[] = {0x0, 0x1};  // Only 1 byte is valid
    std::span<const unsigned char> data(pg_bool);
    field_view fv{data};

    // Act
    auto err = types::parse_binary_bool(fv, b);

    // Assert
    BOOST_TEST_EQ(err, error_code(client_errc::protocol_value_error));
}

// BYTEA
void test_parse_text_bytea_success()
{
    // std::vector as storage
    {
        // Arrange
        std::vector<std::byte> ba_storage(4);
        std::span<std::byte> ba{ba_storage};
        std::string str = "\\x21061977";
        std::span<const unsigned char> data(reinterpret_cast<const unsigned char*>(str.data()), str.size());
        field_view fv{data};

        // Act
        auto err = types::parse_text_bytea(fv, ba);

        // Assert
        BOOST_TEST_EQ(err, std::error_code());
        BOOST_TEST_EQ(ba.size(), 4u);
        std::stringstream ss;
        ss << ba_storage;
        BOOST_TEST_EQ(ss.str(), "0x21061977");
    }

    // std::array as storage
    {
        std::array<std::byte, 4> ba_storage{};
        std::span<std::byte> ba{ba_storage};
        std::string str = "\\x21061977";
        std::span<const unsigned char> data(reinterpret_cast<const unsigned char*>(str.data()), str.size());
        field_view fv{data};

        // Act
        auto err = types::parse_text_bytea(fv, ba);

        // Assert
        BOOST_TEST_EQ(err, std::error_code());
        BOOST_TEST_EQ(ba.size(), 4u);
        std::stringstream ss;
        ss << ba_storage;
        BOOST_TEST_EQ(ss.str(), "0x21061977");
    }
}

void test_parse_binary_bytea_success()
{
    // std::vector as storage
    {
        // Arrange
        std::vector<std::byte> ba_storage(4);
        std::span<std::byte> ba{ba_storage};
        static constexpr unsigned char pg_bytea[] = {0x21, 0x06, 0x19, 0x77};
        std::span<const unsigned char> data(pg_bytea);
        field_view fv{data};

        // Act
        auto err = types::parse_binary_bytea(fv, ba);

        // Assert
        BOOST_TEST_EQ(err, std::error_code());
        BOOST_TEST_EQ(ba.size(), 4u);
        std::stringstream ss;
        ss << ba_storage;
        BOOST_TEST_EQ(ss.str(), "0x21061977");
    }
    // std::array as storage
    {
        // Arrange
        std::array<std::byte, 4> ba_storage;
        std::span<std::byte> ba{ba_storage};
        static constexpr unsigned char pg_bytea[] = {0x21, 0x06, 0x19, 0x77};
        std::span<const unsigned char> data(pg_bytea);
        field_view fv{data};

        // Act
        auto err = types::parse_binary_bytea(fv, ba);

        // Assert
        BOOST_TEST_EQ(err, std::error_code());
        BOOST_TEST_EQ(ba.size(), 4u);
        std::stringstream ss;
        ss << ba_storage;
        BOOST_TEST_EQ(ss.str(), "0x21061977");
    }
}

void test_parse_text_bytea_missing_prefix_error()
{
    // Arrange
    std::vector<std::byte> ba_storage(16);
    std::span<std::byte> ba{ba_storage};
    std::string str = "21061977";  // Missing the \x prefix
    std::span<const unsigned char> data(reinterpret_cast<const unsigned char*>(str.data()), str.size());
    field_view fv{data};

    // Act
    auto err = types::parse_text_bytea(fv, ba);

    // Assert
    BOOST_TEST_EQ(err, error_code(client_errc::protocol_value_error));
}

void test_parse_text_bytea_odd_length_error()
{
    // Arrange
    std::vector<std::byte> ba_storage(16);
    std::span<std::byte> ba{ba_storage};
    std::string str = "\\x210";  // Odd number of hex digits
    std::span<const unsigned char> data(reinterpret_cast<const unsigned char*>(str.data()), str.size());
    field_view fv{data};

    // Act
    auto err = types::parse_text_bytea(fv, ba);

    // Assert
    BOOST_TEST_EQ(err, error_code(client_errc::protocol_value_error));
}

void test_parse_text_bytea_invalid_hex_error()
{
    // Arrange
    std::vector<std::byte> ba_storage(16);
    std::span<std::byte> ba{ba_storage};
    std::string str = "\\xzz";  // Not valid hex digits
    std::span<const unsigned char> data(reinterpret_cast<const unsigned char*>(str.data()), str.size());
    field_view fv{data};

    // Act
    auto err = types::parse_text_bytea(fv, ba);

    // Assert
    BOOST_TEST_EQ(err, error_code(client_errc::protocol_value_error));
}

// "CHAR" (internal single-byte char)
void test_parse_text_char_success()
{
    // Arrange
    char c = '\0';
    std::string str = "z";
    std::span<const unsigned char> data(reinterpret_cast<const unsigned char*>(str.data()), str.size());
    field_view fv{data};

    // Act
    auto err = types::parse_text_char(fv, c);

    // Assert
    BOOST_TEST_EQ(err, std::error_code());
    BOOST_TEST_EQ(c, 'z');
}

void test_parse_binary_char_success()
{
    // Arrange
    char c = '\0';
    static constexpr unsigned char pg_char[] = {0x7a};  // 'z'
    std::span<const unsigned char> data(pg_char);
    field_view fv{data};

    // Act
    auto err = types::parse_binary_char(fv, c);

    // Assert
    BOOST_TEST_EQ(err, std::error_code());
    BOOST_TEST_EQ(c, 'z');
}

// Postgres sends \0 as an empty string, and bytes with the high bit set as \ooo
void test_parse_text_char_special_success()
{
    struct sample
    {
        std::string_view text;
        char expected;
    };
    const sample samples[] = {
        {"",      '\0'                   },
        {"\\",    '\\'                   },
        {"\\303", static_cast<char>(0303)},
        {"\\377", static_cast<char>(0377)},
        {"\\000", '\0'                   },
    };
    for (const auto& smp : samples)
    {
        char c = 'x';
        std::span<const unsigned char> data(
            reinterpret_cast<const unsigned char*>(smp.text.data()),
            smp.text.size()
        );
        auto err = types::parse_text_char(field_view{data}, c);
        BOOST_TEST_EQ(err, std::error_code());
        BOOST_TEST_EQ(c, smp.expected);
    }
}

void test_parse_text_char_invalid_error()
{
    for (std::string_view text : {"ab", "\\40", "\\400", "\\38z", "\\389"})
    {
        char c = 'x';
        std::span<const unsigned char> data(reinterpret_cast<const unsigned char*>(text.data()), text.size());
        auto err = types::parse_text_char(field_view{data}, c);
        BOOST_TEST_EQ(err, error_code(client_errc::protocol_value_error));
        BOOST_TEST_EQ(c, 'x');
    }
}

void test_parse_binary_char_wrong_size_error()
{
    // Arrange
    char c = '\0';
    static constexpr unsigned char pg_char[] = {0x7a, 0x7a};  // Only 1 byte is valid
    std::span<const unsigned char> data(pg_char);
    field_view fv{data};

    // Act
    auto err = types::parse_binary_char(fv, c);

    // Assert
    BOOST_TEST_EQ(err, error_code(client_errc::protocol_value_error));
}

// OID
void test_parse_text_oid_success()
{
    // Arrange
    std::uint32_t o = 0;
    std::string str = "5887";
    std::span<const unsigned char> data(reinterpret_cast<const unsigned char*>(str.data()), str.size());
    field_view fv{data};

    // Act
    auto err = types::parse_text_oid(fv, o);

    // Assert
    BOOST_TEST_EQ(err, std::error_code());
    BOOST_TEST_EQ(o, 5887u);
}

void test_parse_binary_oid_success()
{
    // Arrange
    std::uint32_t o = 0;
    static constexpr unsigned char pg_oid[] = {0x00, 0x00, 0x16, 0xff};  // 5887, big endian
    std::span<const unsigned char> data(pg_oid);
    field_view fv{data};

    // Act
    auto err = types::parse_binary_oid(fv, o);

    // Assert
    BOOST_TEST_EQ(err, std::error_code());
    BOOST_TEST_EQ(o, 5887u);
}

void test_parse_text_oid_garbage_error()
{
    // Arrange
    std::uint32_t o = 0;
    std::string str = "not_an_oid";
    std::span<const unsigned char> data(reinterpret_cast<const unsigned char*>(str.data()), str.size());
    field_view fv{data};

    // Act
    auto err = types::parse_text_oid(fv, o);

    // Assert
    BOOST_TEST_EQ(err, error_code(client_errc::protocol_value_error));
}

void test_parse_binary_oid_wrong_size_error()
{
    // Arrange
    std::uint32_t o = 0;
    static constexpr unsigned char pg_oid[] = {0x00, 0x00, 0x16};  // Only 3 bytes, needs 4
    std::span<const unsigned char> data(pg_oid);
    field_view fv{data};

    // Act
    auto err = types::parse_binary_oid(fv, o);

    // Assert
    BOOST_TEST_EQ(err, error_code(client_errc::protocol_value_error));
}

// INT
template <typename T, T in_val>
void test_parse_text_int_success()
{
    // Arrange
    T out_val;
    std::string str = std::to_string(in_val);
    std::span<const unsigned char> data(reinterpret_cast<const unsigned char*>(str.data()), str.size());
    field_view fv{data};

    // Act
    auto err = types::parse_text_int<T>(fv, out_val);

    // Assert
    BOOST_TEST_EQ(err, std::error_code());
    BOOST_TEST_EQ(out_val, in_val);
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
    BOOST_TEST_EQ(err, std::error_code());
    BOOST_TEST_EQ(out_val, in_val);
}

template <typename T>
void test_parse_text_int_garbage_error()
{
    // Arrange
    T out_val{};
    std::string str = "not_a_number";
    std::span<const unsigned char> data(reinterpret_cast<const unsigned char*>(str.data()), str.size());
    field_view fv{data};

    // Act
    auto err = types::parse_text_int<T>(fv, out_val);

    // Assert
    BOOST_TEST_EQ(err, error_code(client_errc::protocol_value_error));
}

template <typename T>
void test_parse_text_int_overflow_error()
{
    // Arrange
    T out_val{};
    // One more than the maximum representable value for T
    std::string str = std::to_string(static_cast<std::intmax_t>(std::numeric_limits<T>::max()) + 1);
    std::span<const unsigned char> data(reinterpret_cast<const unsigned char*>(str.data()), str.size());
    field_view fv{data};

    // Act
    auto err = types::parse_text_int<T>(fv, out_val);

    // Assert
    BOOST_TEST_EQ(err, error_code(client_errc::protocol_value_error));
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
    BOOST_TEST_EQ(err, error_code(client_errc::protocol_value_error));
}

template <typename T>
void test_parse_binary_int_empty_error()
{
    // Arrange
    T out_val{};
    std::vector<uint8_t> buffer;  // Empty buffer
    field_view fv{buffer};

    // Act
    auto err = types::parse_binary_int<T>(fv, out_val);

    // Assert
    BOOST_TEST_EQ(err, error_code(client_errc::protocol_value_error));
}

template <typename T>
void test_parse_text_number_empty_error()
{
    // Arrange. The output must not be touched
    T out_val = 7;
    std::span<const unsigned char> data;
    field_view fv{data};

    // Act
    error_code err;
    if constexpr (std::is_integral_v<T>)
        err = types::parse_text_int<T>(fv, out_val);
    else
        err = types::parse_text_float<T>(fv, out_val);

    // Assert
    BOOST_TEST_EQ(err, error_code(client_errc::protocol_value_error));
    BOOST_TEST_EQ(out_val, T(7));
}

void test_parse_text_int64_overflow_error()
{
    std::string str = "9223372036854775808";
    std::span<const unsigned char> data(reinterpret_cast<const unsigned char*>(str.data()), str.size());
    std::int64_t out_val = 7;
    auto err = types::parse_text_int<std::int64_t>(field_view{data}, out_val);
    BOOST_TEST_EQ(err, error_code(client_errc::protocol_value_error));
    BOOST_TEST_EQ(out_val, 7);
}

void test_parse_text_bytea_edge_cases()
{
    auto parse = [](std::string_view text, std::span<std::byte> out) {
        std::span<const unsigned char> data(reinterpret_cast<const unsigned char*>(text.data()), text.size());
        return types::parse_text_bytea(field_view{data}, out);
    };

    // Empty value
    {
        std::array<std::byte, 2> out{std::byte{1}, std::byte{2}};
        BOOST_TEST_EQ(parse("\\x", out), error_code());
        BOOST_TEST(out[0] == std::byte{1});
    }

    // Uppercase digits are accepted
    {
        std::array<std::byte, 2> out{};
        BOOST_TEST_EQ(parse("\\xABcd", out), error_code());
        BOOST_TEST(out[0] == std::byte{0xab});
        BOOST_TEST(out[1] == std::byte{0xcd});
    }

    // Destination too small
    {
        std::array<std::byte, 1> out{};
        BOOST_TEST_EQ(parse("\\x0102", out), std::make_error_code(std::errc::result_out_of_range));
    }

    // An invalid digit after valid ones doesn't leave a partial write
    {
        std::array<std::byte, 3> out{std::byte{9}, std::byte{9}, std::byte{9}};
        BOOST_TEST_EQ(parse("\\x0102zz", out), error_code(client_errc::protocol_value_error));
        BOOST_TEST(out[0] == std::byte{9});
        BOOST_TEST(out[1] == std::byte{9});
    }
}

void test_parse_binary_bytea_too_small_error()
{
    static constexpr unsigned char pg_bytea[] = {0x01, 0x02};
    std::array<std::byte, 1> out{};
    auto err = types::parse_binary_bytea(
        field_view{std::span<const unsigned char>(pg_bytea)},
        std::span<std::byte>(out)
    );
    BOOST_TEST_EQ(err, std::make_error_code(std::errc::result_out_of_range));
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
    std::span<const unsigned char> data(reinterpret_cast<const unsigned char*>(str.data()), str.size());
    field_view fv{data};

    // Act
    auto err = types::parse_text_float<T>(fv, out_val);

    // Assert
    BOOST_TEST_EQ(err, std::error_code());
    if (std::isnan(in_val))
    {
        BOOST_TEST(std::isnan(out_val));
    }
    else if (std::isinf(in_val))
    {
        BOOST_TEST(std::isinf(out_val));
        BOOST_TEST_EQ(std::signbit(out_val), std::signbit(in_val));
    }
    else
    {
        BOOST_TEST_EQ(out_val, in_val);
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
    BOOST_TEST_EQ(err, std::error_code());
    if (std::isnan(in_val))
    {
        BOOST_TEST(std::isnan(out_val));
    }
    else if (std::isinf(in_val))
    {
        BOOST_TEST(std::isinf(out_val));
        BOOST_TEST_EQ(std::signbit(out_val), std::signbit(in_val));
    }
    else
    {
        BOOST_TEST_EQ(out_val, in_val);
    }
}

template <std::size_t N>
void test_parse_binary_float_success(const unsigned char (&in_val)[N])
{
    static_assert(N == sizeof(float) || N == sizeof(double), "Unsupported byte width for a PostgreSQL float");
    using T = std::conditional_t<N == sizeof(float), float, double>;

    // Arrange
    T out_val{};
    std::span<const unsigned char> data(in_val);
    field_view fv{data};

    // Act
    auto err = types::parse_binary_float<T>(fv, out_val);

    // Assert
    BOOST_TEST_EQ(err, std::error_code());

    const T expected = boost::endian::endian_load<T, sizeof(T), boost::endian::order::big>(in_val);
    if (std::isnan(expected))
    {
        BOOST_TEST(std::isnan(out_val));
    }
    else if (std::isinf(expected))
    {
        BOOST_TEST(std::isinf(out_val));
        BOOST_TEST_EQ(std::signbit(out_val), std::signbit(expected));
    }
    else
    {
        BOOST_TEST_EQ(out_val, expected);
    }
}

template <typename T>
void test_parse_text_float_garbage_error()
{
    // Arrange
    T out_val{};
    std::string str = "not_a_float";
    std::span<const unsigned char> data(reinterpret_cast<const unsigned char*>(str.data()), str.size());
    field_view fv{data};

    // Act
    auto err = types::parse_text_float<T>(fv, out_val);

    // Assert
    BOOST_TEST_EQ(err, error_code(client_errc::protocol_value_error));
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
    BOOST_TEST_EQ(err, error_code(client_errc::protocol_value_error));
}

template <typename T = std::string>
void test_parse_text_text_success(const T& in_val)
{
    // Arrange
    T out_val;
    const std::string str = in_val;
    std::span<const unsigned char> data(reinterpret_cast<const unsigned char*>(str.data()), str.size());
    field_view fv{data};

    // Act
    auto err = types::parse_text_text<T>(fv, out_val);

    // Assert
    BOOST_TEST_EQ(err, std::error_code());
    BOOST_TEST_EQ(out_val, in_val);
}

template <typename T = std::string>
void test_parse_binary_text_success(const T& in_val)
{
    // Arrange
    T out_val;
    const std::string_view str = in_val;
    std::span<const unsigned char> data(reinterpret_cast<const unsigned char*>(str.data()), str.size());
    field_view fv{data};

    // Act
    auto err = types::parse_binary_text<T>(fv, out_val);

    // Assert
    BOOST_TEST_EQ(err, std::error_code());
    BOOST_TEST_EQ(out_val, in_val);

    // Owns a copy
    BOOST_TEST(static_cast<const void*>(out_val.data()) != static_cast<const void*>(data.data()));
}

//
// field_is_compatible / field_parse (field_traits_base.hpp)
//
void test_field_is_compatible_bool_success()
{
    BOOST_TEST_EQ(field_is_compatible<bool>(detail::bool_oid), std::error_code());
}

void test_field_is_compatible_bool_incompatible_error()
{
    BOOST_TEST_EQ(
        field_is_compatible<bool>(detail::int4_oid),
        error_code(client_errc::incompatible_field_type)
    );
}

void test_field_is_compatible_int_widening_success()
{
    // A smaller wire type is compatible with a wider C++ type
    BOOST_TEST_EQ(field_is_compatible<std::int32_t>(detail::int2_oid), std::error_code());
    BOOST_TEST_EQ(field_is_compatible<std::int64_t>(detail::int4_oid), std::error_code());
}

void test_field_is_compatible_int_narrowing_error()
{
    // A wider wire type is not compatible with a narrower C++ type
    BOOST_TEST_EQ(
        field_is_compatible<std::int16_t>(detail::int4_oid),
        error_code(client_errc::incompatible_field_type)
    );
    BOOST_TEST_EQ(
        field_is_compatible<std::int32_t>(detail::int8_oid),
        error_code(client_errc::incompatible_field_type)
    );
}

void test_field_is_compatible_string_success()
{
    BOOST_TEST_EQ(field_is_compatible<std::string>(detail::text_oid), std::error_code());
    BOOST_TEST_EQ(field_is_compatible<std::string>(detail::varchar_oid), std::error_code());
    BOOST_TEST_EQ(field_is_compatible<std::string>(detail::name_oid), std::error_code());
    BOOST_TEST_EQ(field_is_compatible<std::string>(detail::bpchar_oid), std::error_code());
}

void test_field_is_compatible_char_success()
{
    BOOST_TEST_EQ(field_is_compatible<char>(detail::char_oid), std::error_code());
}

void test_field_is_compatible_char_incompatible_error()
{
    BOOST_TEST_EQ(
        field_is_compatible<char>(detail::text_oid),
        error_code(client_errc::incompatible_field_type)
    );
}

void test_field_is_compatible_oid_success()
{
    BOOST_TEST_EQ(field_is_compatible<std::uint32_t>(detail::oid_oid), std::error_code());
}

void test_field_is_compatible_oid_incompatible_error()
{
    BOOST_TEST_EQ(
        field_is_compatible<std::uint32_t>(detail::int4_oid),
        error_code(client_errc::incompatible_field_type)
    );
}

void test_field_parse_text_unexpected_null_error()
{
    // Arrange
    bool b = false;
    field_view fv;  // NULL

    // Act
    auto err = field_parse(fv, detail::bool_oid, protocol::format_code::text, b);

    // Assert
    BOOST_TEST_EQ(err, error_code(client_errc::unexpected_null));
}

void test_field_parse_binary_unexpected_null_error()
{
    // Arrange
    bool b = false;
    field_view fv;  // NULL

    // Act
    auto err = field_parse(fv, detail::bool_oid, protocol::format_code::binary, b);

    // Assert
    BOOST_TEST_EQ(err, error_code(client_errc::unexpected_null));
}

void test_field_parse_text_bool_success()
{
    // Arrange
    bool b = false;
    std::string str = "t";
    std::span<const unsigned char> data(reinterpret_cast<const unsigned char*>(str.data()), str.size());
    field_view fv{data};

    // Act
    auto err = field_parse(fv, detail::bool_oid, protocol::format_code::text, b);

    // Assert
    BOOST_TEST_EQ(err, std::error_code());
    BOOST_TEST_EQ(b, true);
}

void test_field_parse_text_char_success()
{
    // Arrange
    char c = '\0';
    std::string str = "z";
    std::span<const unsigned char> data(reinterpret_cast<const unsigned char*>(str.data()), str.size());
    field_view fv{data};

    // Act
    auto err = field_parse(fv, detail::char_oid, protocol::format_code::text, c);

    // Assert
    BOOST_TEST_EQ(err, std::error_code());
    BOOST_TEST_EQ(c, 'z');
}

void test_field_parse_text_char_unexpected_null_error()
{
    // Arrange
    char c = '\0';
    field_view fv;  // NULL

    // Act
    auto err = field_parse(fv, detail::char_oid, protocol::format_code::text, c);

    // Assert
    BOOST_TEST_EQ(err, error_code(client_errc::unexpected_null));
}

void test_field_parse_text_oid_success()
{
    // Arrange
    std::uint32_t o = 0;
    std::string str = "5887";
    std::span<const unsigned char> data(reinterpret_cast<const unsigned char*>(str.data()), str.size());
    field_view fv{data};

    // Act
    auto err = field_parse(fv, detail::oid_oid, protocol::format_code::text, o);

    // Assert
    BOOST_TEST_EQ(err, std::error_code());
    BOOST_TEST_EQ(o, 5887u);
}

void test_field_parse_text_oid_unexpected_null_error()
{
    // Arrange
    std::uint32_t o = 0;
    field_view fv;  // NULL

    // Act
    auto err = field_parse(fv, detail::oid_oid, protocol::format_code::text, o);

    // Assert
    BOOST_TEST_EQ(err, error_code(client_errc::unexpected_null));
}

void test_field_parse_binary_int32_from_int2_wire_success()
{
    // Arrange
    std::int32_t out_val = 0;
    static constexpr unsigned char pg_int2[] = {0x00, 0x2a};  // 42, big endian
    std::span<const unsigned char> data(pg_int2);
    field_view fv{data};

    // Act
    auto err = field_parse(fv, detail::int2_oid, protocol::format_code::binary, out_val);

    // Assert
    BOOST_TEST_EQ(err, std::error_code());
    BOOST_TEST_EQ(out_val, 42);
}

//
// Nullable fields (field_traits_nullable.hpp)
//
void test_field_is_compatible_optional_delegates_to_value_type()
{
    // An optional is compatible with whatever its value type is compatible with
    BOOST_TEST_EQ(field_is_compatible<std::optional<std::int32_t>>(detail::int2_oid), error_code());
    BOOST_TEST_EQ(
        field_is_compatible<std::optional<std::int32_t>>(detail::text_oid),
        error_code(client_errc::incompatible_field_type)
    );
}

void test_field_parse_text_optional_null_success()
{
    // Arrange
    std::optional<std::int32_t> out_val = 42;
    field_view fv;  // NULL

    // Act
    auto err = field_parse(fv, detail::int4_oid, protocol::format_code::text, out_val);

    // Assert: a NULL field yields an empty optional, rather than an error
    BOOST_TEST_EQ(err, std::error_code());
    BOOST_TEST(!out_val.has_value());
}

void test_field_parse_binary_optional_null_success()
{
    // Arrange
    std::optional<std::int32_t> out_val = 42;
    field_view fv;  // NULL

    // Act
    auto err = field_parse(fv, detail::int4_oid, protocol::format_code::binary, out_val);

    // Assert: a NULL field yields an empty optional, rather than an error
    BOOST_TEST_EQ(err, std::error_code());
    BOOST_TEST(!out_val.has_value());
}

void test_field_parse_text_optional_non_null_success()
{
    // Arrange
    std::optional<std::int32_t> out_val;
    std::string str = "42";
    std::span<const unsigned char> data(reinterpret_cast<const unsigned char*>(str.data()), str.size());
    field_view fv{data};

    // Act
    auto err = field_parse(fv, detail::int4_oid, protocol::format_code::text, out_val);

    // Assert
    BOOST_TEST_EQ(err, std::error_code());
    BOOST_TEST(out_val.has_value());
    BOOST_TEST_EQ(*out_val, 42);
}

//
// parsable_field / serializable_field
//
static_assert(parsable_field<bool>);
static_assert(parsable_field<std::int32_t>);
static_assert(parsable_field<std::string>);
static_assert(parsable_field<std::span<std::byte>>);
static_assert(parsable_field<std::optional<std::int32_t>>);

// Parsing requires an owning string
static_assert(!parsable_field<std::string_view>);
static_assert(!parsable_field<std::optional<std::string_view>>);

// Types without a mapping are rejected
struct unmapped_type
{
};
static_assert(!parsable_field<unmapped_type>);
static_assert(!serializable_field<unmapped_type>);

static_assert(serializable_field<std::int16_t>);
static_assert(serializable_field<std::int32_t>);
static_assert(serializable_field<std::int64_t>);
static_assert(serializable_field<std::string>);
static_assert(serializable_field<std::string_view>);

static_assert(serializable_field<bool>);
static_assert(serializable_field<char>);
static_assert(serializable_field<float>);
static_assert(serializable_field<double>);
static_assert(serializable_field<std::uint32_t>);
static_assert(serializable_field<std::span<const std::byte>>);

// An optional is serializable if and only if its value type is
static_assert(serializable_field<std::optional<std::int32_t>>);
static_assert(serializable_field<std::optional<std::string>>);
static_assert(serializable_field<std::optional<std::string_view>>);
static_assert(serializable_field<std::optional<bool>>);
static_assert(!serializable_field<std::optional<unmapped_type>>);

// An optional advertises the OID of its value type
static_assert(field_serialize_oid<std::optional<std::int32_t>> == field_serialize_oid<std::int32_t>);
static_assert(field_serialize_oid<std::optional<std::string>> == field_serialize_oid<std::string>);

//
// field_serialize (field_traits_base.hpp)
//

// Serializes value and checks the resulting bytes
template <class T>
void check_serialize(
    const T& value,
    protocol::format_code code,
    std::initializer_list<unsigned char> expected,
    std::source_location loc = std::source_location::current()
)
{
    // Arrange. Pre-populate the buffer, to verify that we append rather than overwrite
    static constexpr unsigned char prefix[] = {0xde, 0xad};
    std::vector<unsigned char> to(std::begin(prefix), std::end(prefix));

    // Act
    auto err = field_serialize(value, code, to);

    // Assert
    BOOST_TEST_EQ(err, std::error_code());
    std::vector<unsigned char> expected_buff(std::begin(prefix), std::end(prefix));
    expected_buff.insert(expected_buff.end(), expected.begin(), expected.end());
    test::test_range_eq(to, expected_buff, loc);
}

void test_field_serialize_int_text_success()
{
    check_serialize(std::int16_t(-32768), protocol::format_code::text, {'-', '3', '2', '7', '6', '8'});
    check_serialize(std::int16_t(32767), protocol::format_code::text, {'3', '2', '7', '6', '7'});
    check_serialize(std::int32_t(0), protocol::format_code::text, {'0'});
    check_serialize(
        std::int32_t(-2147483648),
        protocol::format_code::text,
        {'-', '2', '1', '4', '7', '4', '8', '3', '6', '4', '8'}
    );
    check_serialize(
        (std::numeric_limits<std::int64_t>::max)(),
        protocol::format_code::text,
        {'9', '2', '2', '3', '3', '7', '2', '0', '3', '6', '8', '5', '4', '7', '7', '5', '8', '0', '7'}
    );
}

void test_field_serialize_int_binary_success()
{
    // Big endian, using the type's own width
    check_serialize(std::int16_t(42), protocol::format_code::binary, {0x00, 0x2a});
    check_serialize(std::int16_t(-2), protocol::format_code::binary, {0xff, 0xfe});
    check_serialize(std::int32_t(42), protocol::format_code::binary, {0x00, 0x00, 0x00, 0x2a});
    check_serialize(
        std::int64_t(42),
        protocol::format_code::binary,
        {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2a}
    );
    check_serialize(
        (std::numeric_limits<std::int64_t>::min)(),
        protocol::format_code::binary,
        {0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}
    );
}

void test_field_serialize_string_success()
{
    // Strings are serialized identically in both formats: the raw bytes, unterminated
    for (auto code : {protocol::format_code::text, protocol::format_code::binary})
    {
        check_serialize(std::string("abc"), code, {'a', 'b', 'c'});
        check_serialize(std::string_view("abc"), code, {'a', 'b', 'c'});
        check_serialize(std::string(), code, {});

        // A string literal is a C array, which the traits accept without decaying it.
        // The terminator is not part of the value
        check_serialize("abc", code, {'a', 'b', 'c'});

        // Embedded NULLs are preserved (no C-string semantics)
        check_serialize(std::string("a\0b", 3), code, {'a', 0x00, 'b'});
    }
}

void test_field_serialize_bool_success()
{
    check_serialize(true, protocol::format_code::text, {'t'});
    check_serialize(false, protocol::format_code::text, {'f'});
    check_serialize(true, protocol::format_code::binary, {0x01});
    check_serialize(false, protocol::format_code::binary, {0x00});
}

void test_field_serialize_bytea_success()
{
    std::vector<std::byte> storage{std::byte{0x00}, std::byte{0x0f}, std::byte{0xab}, std::byte{0xff}};
    const std::span<const std::byte> value{storage};
    check_serialize(value, protocol::format_code::text, {'\\', 'x', '0', '0', '0', 'f', 'a', 'b', 'f', 'f'});
    check_serialize(value, protocol::format_code::binary, {0x00, 0x0f, 0xab, 0xff});

    // Empty values
    check_serialize(std::span<const std::byte>{}, protocol::format_code::text, {'\\', 'x'});
    check_serialize(std::span<const std::byte>{}, protocol::format_code::binary, {});
}

void test_field_serialize_char_success()
{
    for (auto code : {protocol::format_code::text, protocol::format_code::binary})
    {
        check_serialize('a', code, {'a'});
        check_serialize('\0', code, {0x00});
    }
}

void test_field_serialize_oid_success()
{
    check_serialize(std::uint32_t(42), protocol::format_code::text, {'4', '2'});
    check_serialize(
        std::uint32_t(4294967295u),
        protocol::format_code::text,
        {'4', '2', '9', '4', '9', '6', '7', '2', '9', '5'}
    );
    check_serialize(std::uint32_t(42), protocol::format_code::binary, {0x00, 0x00, 0x00, 0x2a});
    check_serialize(std::uint32_t(4294967295u), protocol::format_code::binary, {0xff, 0xff, 0xff, 0xff});
}

void test_field_serialize_float_text_success()
{
    check_serialize(1.5f, protocol::format_code::text, {'1', '.', '5'});
    check_serialize(-0.25f, protocol::format_code::text, {'-', '0', '.', '2', '5'});
    check_serialize(0.1, protocol::format_code::text, {'0', '.', '1'});
    check_serialize(std::numeric_limits<double>::infinity(), protocol::format_code::text, {'i', 'n', 'f'});
    check_serialize(std::numeric_limits<double>::quiet_NaN(), protocol::format_code::text, {'n', 'a', 'n'});
}

void test_field_serialize_float_binary_success()
{
    check_serialize(1.5f, protocol::format_code::binary, {0x3f, 0xc0, 0x00, 0x00});
    check_serialize(-2.0f, protocol::format_code::binary, {0xc0, 0x00, 0x00, 0x00});
    check_serialize(1.5, protocol::format_code::binary, {0x3f, 0xf8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
    check_serialize(-2.0, protocol::format_code::binary, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
    check_serialize(
        std::numeric_limits<double>::infinity(),
        protocol::format_code::binary,
        {0x7f, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}
    );
}

void test_field_serialize_float_nan_sign()
{
    for (auto code : {protocol::format_code::text})
        check_serialize(-std::numeric_limits<double>::quiet_NaN(), code, {'n', 'a', 'n'});
}

void test_field_serialize_bytea_const_span()
{
    const std::array<std::byte, 2> storage{std::byte{0x01}, std::byte{0xff}};
    const std::span<const std::byte> value{storage};
    check_serialize(value, protocol::format_code::text, {'\\', 'x', '0', '1', 'f', 'f'});
    check_serialize(value, protocol::format_code::binary, {0x01, 0xff});
}

void test_field_parse_widening_success()
{
    auto fv_of = [](const auto& buf) { return field_view{std::span<const unsigned char>(buf)}; };

    // int2 -> int8, int4 -> int8, float4 -> float8
    {
        const unsigned char buf[] = {0xff, 0xfe};
        std::int64_t out = 0;
        BOOST_TEST_EQ(
            field_parse(fv_of(buf), detail::int2_oid, protocol::format_code::binary, out),
            error_code()
        );
        BOOST_TEST_EQ(out, -2);
    }
    {
        const unsigned char buf[] = {0x00, 0x00, 0x01, 0x00};
        std::int64_t out = 0;
        BOOST_TEST_EQ(
            field_parse(fv_of(buf), detail::int4_oid, protocol::format_code::binary, out),
            error_code()
        );
        BOOST_TEST_EQ(out, 256);
    }
    {
        const unsigned char buf[] = {0x3f, 0xc0, 0x00, 0x00};
        double out = 0;
        BOOST_TEST_EQ(
            field_parse(fv_of(buf), detail::float4_oid, protocol::format_code::binary, out),
            error_code()
        );
        BOOST_TEST_EQ(out, 1.5);
    }
}

void test_field_parse_widening_error_keeps_output()
{
    const unsigned char buf[] = {0x01};  // Wrong size for int2
    field_view fv{std::span<const unsigned char>(buf)};
    std::int32_t out32 = 7;
    std::int64_t out64 = 7;
    double outd = 7;
    BOOST_TEST_EQ(
        field_parse(fv, detail::int2_oid, protocol::format_code::binary, out32),
        error_code(client_errc::protocol_value_error)
    );
    BOOST_TEST_EQ(
        field_parse(fv, detail::int4_oid, protocol::format_code::binary, out64),
        error_code(client_errc::protocol_value_error)
    );
    BOOST_TEST_EQ(
        field_parse(fv, detail::float4_oid, protocol::format_code::binary, outd),
        error_code(client_errc::protocol_value_error)
    );
    BOOST_TEST_EQ(out32, 7);
    BOOST_TEST_EQ(out64, 7);
    BOOST_TEST_EQ(outd, 7.0);
}

void test_field_parse_vector_bytea_success()
{
    {
        const unsigned char buf[] = {0x01, 0x02, 0x03};
        std::vector<std::byte> out;
        BOOST_TEST_EQ(
            field_parse(
                field_view{std::span<const unsigned char>(buf)},
                detail::bytea_oid,
                protocol::format_code::binary,
                out
            ),
            error_code()
        );
        BOOST_TEST_EQ(out.size(), 3u);
        BOOST_TEST(out[2] == std::byte{3});
    }
    {
        std::string_view text = "\\x0aff";
        std::span<const unsigned char> data(reinterpret_cast<const unsigned char*>(text.data()), text.size());
        std::vector<std::byte> out;
        BOOST_TEST_EQ(
            field_parse(field_view{data}, detail::bytea_oid, protocol::format_code::text, out),
            error_code()
        );
        BOOST_TEST_EQ(out.size(), 2u);
        BOOST_TEST(out[0] == std::byte{0x0a});
        BOOST_TEST(out[1] == std::byte{0xff});
    }
}

void test_field_is_compatible_more_types()
{
    BOOST_TEST_EQ(field_is_compatible<std::int64_t>(detail::int8_oid), error_code());
    BOOST_TEST_EQ(field_is_compatible<std::int64_t>(detail::int2_oid), error_code());
    BOOST_TEST_EQ(field_is_compatible<float>(detail::float4_oid), error_code());
    BOOST_TEST_EQ(
        field_is_compatible<float>(detail::float8_oid),
        error_code(client_errc::incompatible_field_type)
    );
    BOOST_TEST_EQ(field_is_compatible<double>(detail::float4_oid), error_code());
    BOOST_TEST_EQ(field_is_compatible<std::span<std::byte>>(detail::bytea_oid), error_code());
    BOOST_TEST_EQ(field_is_compatible<std::vector<std::byte>>(detail::bytea_oid), error_code());
    BOOST_TEST_EQ(
        field_is_compatible<std::span<std::byte>>(detail::text_oid),
        error_code(client_errc::incompatible_field_type)
    );
}

// Serialized values parse back to the same value
void test_serialize_parse_roundtrip()
{
    for (auto code : {protocol::format_code::text, protocol::format_code::binary})
    {
        {
            std::vector<unsigned char> buff;
            BOOST_TEST_EQ(field_serialize(0.1, code, buff), error_code());
            double out{};
            BOOST_TEST_EQ(field_parse(field_view(buff), detail::float8_oid, code, out), error_code());
            BOOST_TEST_EQ(out, 0.1);
        }
        {
            std::vector<unsigned char> buff;
            std::array<std::byte, 3> in_storage{std::byte{0x00}, std::byte{0x7f}, std::byte{0xff}};
            std::span<const std::byte> in{in_storage};
            BOOST_TEST_EQ(field_serialize(in, code, buff), error_code());
            std::array<std::byte, 3> out_storage{};
            std::span<std::byte> out{out_storage};
            BOOST_TEST_EQ(field_parse(field_view(buff), detail::bytea_oid, code, out), error_code());
            BOOST_TEST(out_storage == in_storage);
        }
        {
            std::vector<unsigned char> buff;
            std::vector<std::byte> in_storage{std::byte{0x00}, std::byte{0x7f}, std::byte{0xff}};
            std::span<const std::byte> in{in_storage};
            BOOST_TEST_EQ(field_serialize(in, code, buff), error_code());
            std::vector<std::byte> out_storage(in_storage.size());
            std::span<std::byte> out{out_storage};
            BOOST_TEST_EQ(field_parse(field_view(buff), detail::bytea_oid, code, out), error_code());
            BOOST_TEST(out_storage == in_storage);
        }
        {
            std::vector<unsigned char> buff;
            BOOST_TEST_EQ(field_serialize(std::int64_t(-42), code, buff), error_code());
            std::int64_t out = 0;
            BOOST_TEST_EQ(field_parse(field_view(buff), detail::int8_oid, code, out), error_code());
            BOOST_TEST_EQ(out, -42);
        }
        {
            std::vector<unsigned char> buff;
            BOOST_TEST_EQ(field_serialize(std::uint32_t(4000000000u), code, buff), error_code());
            std::uint32_t out = 0;
            BOOST_TEST_EQ(field_parse(field_view(buff), detail::oid_oid, code, out), error_code());
            BOOST_TEST_EQ(out, 4000000000u);
        }
        {
            std::vector<unsigned char> buff;
            BOOST_TEST_EQ(field_serialize(std::string("hello"), code, buff), error_code());
            std::string out;
            BOOST_TEST_EQ(field_parse(field_view(buff), detail::text_oid, code, out), error_code());
            BOOST_TEST_EQ(out, "hello");
        }
        {
            std::vector<unsigned char> buff;
            BOOST_TEST_EQ(field_serialize('z', code, buff), error_code());
            char out = 0;
            BOOST_TEST_EQ(field_parse(field_view(buff), detail::char_oid, code, out), error_code());
            BOOST_TEST_EQ(out, 'z');
        }
        {
            std::vector<unsigned char> buff;
            std::array<std::byte, 3> in_storage{std::byte{0x00}, std::byte{0x7f}, std::byte{0xff}};
            BOOST_TEST_EQ(field_serialize(std::span<const std::byte>(in_storage), code, buff), error_code());
            std::vector<std::byte> out;
            BOOST_TEST_EQ(field_parse(field_view(buff), detail::bytea_oid, code, out), error_code());
            BOOST_TEST_EQ(out.size(), 3u);
            BOOST_TEST(out[1] == std::byte{0x7f});
        }
        {
            std::vector<unsigned char> buff;
            BOOST_TEST_EQ(field_serialize(true, code, buff), error_code());
            bool out = false;
            BOOST_TEST_EQ(field_parse(field_view(buff), detail::bool_oid, code, out), error_code());
            BOOST_TEST(out);
        }
    }
}

static_assert(field_serialize_oid<bool> == 16);
static_assert(field_serialize_oid<std::span<const std::byte>> == 17);
static_assert(field_serialize_oid<char> == 18);
static_assert(field_serialize_oid<float> == 700);
static_assert(field_serialize_oid<double> == 701);
static_assert(field_serialize_oid<std::uint32_t> == 26);

//
// Nullable serialization (field_traits_nullable.hpp)
//
void test_field_serialize_optional_engaged_success()
{
    // An engaged optional produces exactly what the underlying value would
    check_serialize(std::optional<std::int32_t>(42), protocol::format_code::text, {'4', '2'});
    check_serialize(std::optional<std::int32_t>(42), protocol::format_code::binary, {0x00, 0x00, 0x00, 0x2a});
    check_serialize(std::optional<std::string>("abc"), protocol::format_code::text, {'a', 'b', 'c'});
}

void test_field_serialize_optional_null()
{
    for (auto code : {protocol::format_code::text, protocol::format_code::binary})
    {
        // Arrange
        std::optional<std::int32_t> value;
        std::vector<unsigned char> to{0xde, 0xad};

        // Act
        auto err = field_serialize(value, code, to);

        // Assert: the sentinel is reported, and nothing is appended
        BOOST_TEST_EQ(err, error_code(client_errc::serialize_null));
        BOOST_TEST_EQ(to.size(), 2u);
    }
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
    test_parse_text_bytea_edge_cases();
    test_parse_binary_bytea_too_small_error();

    // CHAR
    test_parse_text_char_success();
    test_parse_binary_char_success();
    test_parse_text_char_special_success();
    test_parse_text_char_invalid_error();
    test_parse_binary_char_wrong_size_error();

    // OID
    test_parse_text_oid_success();
    test_parse_binary_oid_success();
    test_parse_text_oid_garbage_error();
    test_parse_binary_oid_wrong_size_error();

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
    test_parse_text_int64_overflow_error();
    test_parse_text_number_empty_error<std::int16_t>();
    test_parse_text_number_empty_error<std::int32_t>();
    test_parse_text_number_empty_error<std::int64_t>();
    test_parse_text_number_empty_error<float>();
    test_parse_text_number_empty_error<double>();
    test_parse_binary_int_empty_error<std::int16_t>();
    test_parse_binary_int_empty_error<std::int32_t>();
    test_parse_binary_int_empty_error<std::int64_t>();
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

    // field_is_compatible / field_parse
    test_field_is_compatible_bool_success();
    test_field_is_compatible_bool_incompatible_error();
    test_field_is_compatible_int_widening_success();
    test_field_is_compatible_int_narrowing_error();
    test_field_is_compatible_string_success();
    test_field_is_compatible_char_success();
    test_field_is_compatible_char_incompatible_error();
    test_field_is_compatible_oid_success();
    test_field_is_compatible_oid_incompatible_error();
    test_field_parse_text_unexpected_null_error();
    test_field_parse_binary_unexpected_null_error();
    test_field_parse_text_bool_success();
    test_field_parse_text_char_success();
    test_field_parse_text_char_unexpected_null_error();
    test_field_parse_text_oid_success();
    test_field_parse_text_oid_unexpected_null_error();
    test_field_parse_binary_int32_from_int2_wire_success();

    // Nullable fields
    test_field_is_compatible_optional_delegates_to_value_type();
    test_field_parse_text_optional_null_success();
    test_field_parse_binary_optional_null_success();
    test_field_parse_text_optional_non_null_success();

    // field_serialize
    test_field_serialize_int_text_success();
    test_field_serialize_int_binary_success();
    test_field_serialize_string_success();
    test_field_serialize_bool_success();
    test_field_serialize_bytea_success();
    test_field_serialize_char_success();
    test_field_serialize_oid_success();
    test_field_serialize_float_text_success();
    test_field_serialize_float_binary_success();
    test_serialize_parse_roundtrip();
    test_field_serialize_float_nan_sign();
    test_field_serialize_bytea_const_span();
    test_field_parse_widening_success();
    test_field_parse_widening_error_keeps_output();
    test_field_parse_vector_bytea_success();
    test_field_is_compatible_more_types();
    test_field_serialize_optional_engaged_success();
    test_field_serialize_optional_null();

    return boost::report_errors();
}
