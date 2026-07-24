//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_TYPES_BASE_HPP
#define NATIVEPG_TYPES_BASE_HPP

#include <boost/endian/conversion.hpp>
#include <boost/system/error_code.hpp>

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <format>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "nativepg/client_errc.hpp"
#include "nativepg/field_view.hpp"

namespace nativepg {
namespace types {

using boost::system::error_code;

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
error_code parse_text_to_number(const std::string_view& sv, T& to)
{
    T result = 0;
    auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), result);
    if (ec == std::errc{} && ptr == sv.data() + sv.size())
        to = std::move(result);  // Success
    else if (sv.size() != 0)
        return client_errc::protocol_value_error;

    return {};
}

}  // namespace detail

// BOOL => bool;
template <class T = bool>
error_code parse_text_bool(const field_view& from, T& to)
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
error_code parse_binary_bool(const field_view& from, T& to)
{
    if (from.data().size() != 1)
        return client_errc::protocol_value_error;
    to = from.data()[0] != 0;
    return {};
}

// BYTEA => std::vector<std::byte>>;
template <class T = std::vector<std::byte>>
error_code parse_text_bytea(const field_view& from, T& to)
{
    // PostgresSQL text format for bytea is \x followed by hex pairs
    std::string_view sv = from.data_str();
    if (sv.size() < 2 || sv[0] != '\\' || sv[1] != 'x')
        return client_errc::protocol_value_error;
    sv.remove_prefix(2);
    if (sv.size() % 2 != 0)
        return client_errc::protocol_value_error;
    to.clear();
    to.reserve(sv.size() / 2);
    for (std::size_t i = 0; i < sv.size(); i += 2)
    {
        unsigned char byte{};
        if (auto [ptr, ec] = std::from_chars(sv.data() + i, sv.data() + i + 2, byte, 16);
            ec != std::errc{} || ptr != sv.data() + i + 2)
            return client_errc::protocol_value_error;
        to.push_back(static_cast<std::byte>(byte));
    }
    return {};
}

template <class T = std::vector<std::byte>>
error_code parse_binary_bytea(const field_view& from, T& to)
{
    // Binary format is raw bytes — copy directly
    to.clear();
    to.reserve(from.data().size());
    std::ranges::transform(from.data(), std::back_inserter(to), [](unsigned char byte) {
        return static_cast<std::byte>(byte);
    });
    return {};
}

// "CHAR" => char (INTERNAL CHAR NOT CHAR(N) / CHARACTER(N)!)
template <class T>
error_code parse_text_char(const field_view& from, T& to)
{
    const std::string_view sv = from.data_str();
    if (sv.size() != 1)
        return client_errc::protocol_value_error;
    to = static_cast<char>(sv[0]);
    return {};
}

template <class T>
error_code parse_binary_char(const field_view& from, T& to)
{
    if (from.data().size() != 1)
        return client_errc::protocol_value_error;
    to = boost::endian::endian_load<T, sizeof(T), boost::endian::order::big>(from.data().data());
    return {};
}

// INT => std::int_t
template <class T>
error_code parse_text_int(const field_view& from, T& to)
{
    const std::string_view sv = from.data_str();
    return detail::parse_text_to_number<T>(sv, to);
}

template <class T>
error_code parse_binary_int(const field_view& from, T& to)
{
    if (from.data().size() != sizeof(T))
        return client_errc::protocol_value_error;
    to = boost::endian::endian_load<T, sizeof(T), boost::endian::order::big>(from.data().data());
    return {};
}

// FLOAT => float
template <class T>
error_code parse_text_float(const field_view& from, T& to)
{
    const std::string_view sv = from.data_str();
    return detail::parse_text_to_number<T>(sv, to);
}

template <class T>
error_code parse_binary_float(const field_view& from, T& to)
{
    if (from.data().size() != sizeof(T))
        return client_errc::protocol_value_error;

    to = boost::endian::endian_load<T, sizeof(T), boost::endian::order::big>(from.data().data());
    return {};
}

// TEXT | VARCHAR => std::string
template <class T = std::string>
error_code parse_text_text(const field_view& from, T& to)
{
    to.assign(from.data_str());
    return {};
}

template <class T = std::string>
error_code parse_binary_text(const field_view& from, T& to)
{
    // TODO What about different text encodings?
    to.assign(from.data_str());
    return {};
}

// OID => std::uint32_t
template <class T = std::uint32_t>
error_code parse_text_oid(const field_view& from, T& to)
{
    const std::string_view sv = from.data_str();
    return detail::parse_text_to_number<T>(sv, to);
}

template <class T = std::uint32_t>
error_code parse_binary_oid(const field_view& from, T& to)
{
    if (from.data().size() != sizeof(T))
        return client_errc::protocol_value_error;
    to = boost::endian::endian_load<T, sizeof(T), boost::endian::order::big>(from.data().data());
    return {};
}

}  // namespace types
}  // namespace nativepg

#endif  // NATIVEPG_TYPES_BASE_HPP
