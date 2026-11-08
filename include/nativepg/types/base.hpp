//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_TYPES_BASE_HPP
#define NATIVEPG_TYPES_BASE_HPP

#include <boost/endian/conversion.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <vector>

#include "nativepg/client_errc.hpp"
#include "nativepg/field_view.hpp"

namespace nativepg {
namespace types {

// clang-format off
/*
 Type mapping
| Type      | Category | OID  | C++ type                                | Storage size                                                                |
|-----------|----------|------|-----------------------------------------|-----------------------------------------------------------------------------|
| bool      | base     | 16   | bool                                    | 1 byte                                                                      |
| bytea     | base     | 17   | std::vector<std::byte>                  | variable (1 or 4 bytes header + actual binary data)                         |
| char      | base     | 18   | char                                    | 1 character                                                                 |
| int2      | base     | 21   | std::int16_t                            | 2 bytes                                                                     |
| int4      | base     | 23   | std::int32_t                            | 4 bytes                                                                     |
| int8      | base     | 20   | std::int64_t                            | 8 bytes                                                                     |
| float4    | base     | 700  | float                                   | 4 bytes                                                                     |
| float8    | base     | 701  | double                                  | 8 bytes                                                                     |
| name      | base     | 19   | std::string                             | variable (1 or 4 bytes header + actual string data)                         |
| oid       | base     | 26   | std::uint32_t                           | 4 bytes                                                                     |
| text      | base     | 25   | std::string                             | variable (1 or 4 bytes header + actual string data)                         |
| bpchar    | base     | 1042 | std::string                             | variable (1 or 4 bytes header + actual string data)                         |
| varchar   | base     | 1043 | std::string                             | variable (1 or 4 bytes header + actual string data)                         |
*/
// clang-format on

namespace detail {

template <typename T>
std::error_code parse_text_to_number(const std::string_view& sv, T& to)
{
    T result = 0;
    auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), result);
    if (ec == std::errc{} && sv.size() > 0 && ptr == sv.data() + sv.size())
        to = result;  // Success
    else
        return client_errc::protocol_value_error;

    return {};
}

// Returns -1 if c is not a hex digit
constexpr int hex_value(char c) noexcept
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

}  // namespace detail

// BOOL => bool;
template <class T = bool>
std::error_code parse_text_bool(const field_view& from, T& to)
{
    if (const std::string_view sv = from.data_str(); sv == "t")
        to = true;
    else if (sv == "f")
        to = false;
    else
        return client_errc::protocol_value_error;
    return {};
}

template <class T = bool>
std::error_code parse_binary_bool(const field_view& from, T& to)
{
    if (from.data().size() != 1)
        return client_errc::protocol_value_error;
    to = from.data()[0] != 0;
    return {};
}

inline std::error_code serialize_text_bool(bool value, std::vector<unsigned char>& to)
{
    to.push_back(value ? 't' : 'f');
    return {};
}

inline std::error_code serialize_binary_bool(bool value, std::vector<unsigned char>& to)
{
    to.push_back(value ? 1 : 0);
    return {};
}

// BYTEA => std::span<std::byte>;
// If the destination is larger than the value, only the first bytes are written.
// Text parsing leaves the destination untouched on error.
template <class T = std::span<std::byte>>
std::error_code parse_text_bytea(const field_view& from, T to)
{
    // PostgreSQL text format for bytea is \x followed by hex pairs
    std::string_view sv = from.data_str();
    if (sv.size() < 2 || sv[0] != '\\' || sv[1] != 'x')
        return client_errc::protocol_value_error;

    sv.remove_prefix(2);
    if (sv.size() % 2 != 0)
        return client_errc::protocol_value_error;

    // Calculate required capacity
    const std::size_t required_size = sv.size() / 2;

    // Check if the provided span has enough pre-allocated space
    if (to.size() < required_size)
        return std::make_error_code(std::errc::result_out_of_range);

    // Validate first, so that errors don't leave a partially written destination
    for (const char c : sv)
    {
        if (detail::hex_value(c) < 0)
            return client_errc::protocol_value_error;
    }

    for (std::size_t i = 0; i < required_size; ++i)
    {
        to[i] = static_cast<std::byte>(
            (detail::hex_value(sv[2 * i]) << 4) | detail::hex_value(sv[2 * i + 1])
        );
    }

    return {};
}

template <class T = std::span<std::byte>>
std::error_code parse_binary_bytea(const field_view& from, T to)
{
    if (to.size() < from.data().size())
        return std::make_error_code(std::errc::result_out_of_range);

    // Binary format is raw bytes
    if (!from.data().empty())
        std::memcpy(to.data(), from.data().data(), from.data().size());
    return {};
}

inline std::error_code serialize_text_bytea(std::span<const std::byte> value, std::vector<unsigned char>& to)
{
    constexpr char hex_digits[] = "0123456789abcdef";
    to.reserve(to.size() + 2 + value.size() * 2);
    to.push_back('\\');
    to.push_back('x');
    for (const std::byte b : value)
    {
        const auto v = static_cast<unsigned>(b);
        to.push_back(static_cast<unsigned char>(hex_digits[v >> 4]));
        to.push_back(static_cast<unsigned char>(hex_digits[v & 0xf]));
    }
    return {};
}

inline std::error_code serialize_binary_bytea(
    std::span<const std::byte> value,
    std::vector<unsigned char>& to
)
{
    to.reserve(to.size() + value.size());
    for (const std::byte b : value)
        to.push_back(static_cast<unsigned char>(b));
    return {};
}

// "CHAR" => char (INTERNAL CHAR NOT CHAR(N) / CHARACTER(N)!)
template <class T>
    requires(std::integral<T> && sizeof(T) == 1)
std::error_code parse_text_char(const field_view& from, T& to)
{
    // Empty means \0. Bytes with the high bit set are sent as \ooo (octal). Anything else is sent as is
    const std::string_view sv = from.data_str();
    if (sv.empty())
    {
        to = 0;
        return {};
    }
    if (sv.size() == 1)
    {
        to = static_cast<T>(sv[0]);
        return {};
    }
    if (sv.size() == 4 && sv[0] == '\\' && sv[1] >= '0' && sv[1] <= '3' && sv[2] >= '0' && sv[2] <= '7' &&
        sv[3] >= '0' && sv[3] <= '7')
    {
        to = static_cast<T>(((sv[1] - '0') << 6) | ((sv[2] - '0') << 3) | (sv[3] - '0'));
        return {};
    }
    return client_errc::protocol_value_error;
}

template <class T>
    requires(std::integral<T> && sizeof(T) == 1)
std::error_code parse_binary_char(const field_view& from, T& to)
{
    if (from.data().size() != 1)
        return client_errc::protocol_value_error;
    to = static_cast<T>(from.data()[0]);
    return {};
}

inline std::error_code serialize_text_char(char value, std::vector<unsigned char>& to)
{
    to.push_back(static_cast<unsigned char>(value));
    return {};
}

inline std::error_code serialize_binary_char(char value, std::vector<unsigned char>& to)
{
    to.push_back(static_cast<unsigned char>(value));
    return {};
}

// INT => std::int_t
template <class T>
std::error_code parse_text_int(const field_view& from, T& to)
{
    const std::string_view sv = from.data_str();
    return detail::parse_text_to_number<T>(sv, to);
}

template <std::integral T>
    requires(!std::same_as<T, bool>)
std::error_code parse_binary_int(const field_view& from, T& to)
{
    if (from.data().size() != sizeof(T))
        return client_errc::protocol_value_error;
    to = boost::endian::endian_load<T, sizeof(T), boost::endian::order::big>(from.data().data());
    return {};
}

template <std::integral T>
    requires(!std::same_as<T, bool>)
std::error_code serialize_text_int(const T& value, std::vector<unsigned char>& to)
{
    // Enough for the sign and digits of any integral type up to 64 bits
    char buffer[24];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    if (result.ec != std::errc{})
        return std::make_error_code(result.ec);
    to.insert(to.end(), buffer, result.ptr);
    return {};
}

template <std::integral T>
    requires(!std::same_as<T, bool>)
std::error_code serialize_binary_int(const T& value, std::vector<unsigned char>& to)
{
    constexpr std::size_t size = sizeof(T);
    auto offset = to.size();
    to.resize(offset + size);
    boost::endian::endian_store<T, size, boost::endian::order::big>(to.data() + offset, value);
    return {};
}

// FLOAT => float
template <std::floating_point T>
std::error_code parse_text_float(const field_view& from, T& to)
{
    const std::string_view sv = from.data_str();
    return detail::parse_text_to_number<T>(sv, to);
}

template <std::floating_point T>
    requires(sizeof(T) == 4 || sizeof(T) == 8)
std::error_code parse_binary_float(const field_view& from, T& to)
{
    if (from.data().size() != sizeof(T))
        return client_errc::protocol_value_error;

    using bits_type = std::conditional_t<sizeof(T) == 4, std::uint32_t, std::uint64_t>;
    to = std::bit_cast<T>(
        boost::endian::endian_load<bits_type, sizeof(T), boost::endian::order::big>(from.data().data())
    );
    return {};
}

template <std::floating_point T>
std::error_code serialize_text_float(const T& value, std::vector<unsigned char>& to)
{
    // to_chars may emit "-nan", which Postgres rejects
    if (std::isnan(value))
    {
        constexpr std::string_view nan = "nan";
        to.insert(to.end(), nan.begin(), nan.end());
        return {};
    }

    // Shortest representation that round-trips. Also yields inf and -inf, which Postgres accepts
    char buffer[64];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    if (result.ec != std::errc{})
        return std::make_error_code(result.ec);
    to.insert(to.end(), buffer, result.ptr);
    return {};
}

// float4 and float8 are sent as their IEEE 754 bit pattern in network byte order
template <std::floating_point T>
    requires(sizeof(T) == 4 || sizeof(T) == 8)
std::error_code serialize_binary_float(const T& value, std::vector<unsigned char>& to)
{
    using bits_type = std::conditional_t<sizeof(T) == 4, std::uint32_t, std::uint64_t>;
    const auto offset = to.size();
    to.resize(offset + sizeof(T));
    boost::endian::endian_store<bits_type, sizeof(T), boost::endian::order::big>(
        to.data() + offset,
        std::bit_cast<bits_type>(value)
    );
    return {};
}

// TEXT | VARCHAR => std::string
template <class T = std::string>
std::error_code parse_text_text(const field_view& from, T& to)
{
    const auto sv = from.data_str();
    to.assign(sv.data(), sv.size());
    return {};
}

template <class T = std::string>
std::error_code parse_binary_text(const field_view& from, T& to)
{
    // TODO What about different text encodings?
    const auto sv = from.data_str();
    to.assign(sv.data(), sv.size());
    return {};
}

inline std::error_code serialize_text_text(std::string_view value, std::vector<unsigned char>& to)
{
    to.insert(to.end(), value.begin(), value.end());
    return {};
}

inline std::error_code serialize_binary_text(std::string_view value, std::vector<unsigned char>& to)
{
    to.insert(to.end(), value.begin(), value.end());
    return {};
}

// OID => std::uint32_t
template <class T = std::uint32_t>
std::error_code parse_text_oid(const field_view& from, T& to)
{
    const std::string_view sv = from.data_str();
    return detail::parse_text_to_number<T>(sv, to);
}

template <class T = std::uint32_t>
std::error_code parse_binary_oid(const field_view& from, T& to)
{
    if (from.data().size() != sizeof(T))
        return client_errc::protocol_value_error;
    to = boost::endian::endian_load<T, sizeof(T), boost::endian::order::big>(from.data().data());
    return {};
}

}  // namespace types
}  // namespace nativepg

#endif  // NATIVEPG_TYPES_BASE_HPP
