//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_SQLSTATE_HPP
#define NATIVEPG_SQLSTATE_HPP

#include <cstddef>
#include <stdexcept>
#include <string_view>
#include <system_error>

namespace nativepg {

namespace detail {

constexpr int parse_sqlstate_char(char value)
{
    // '0' => 0
    // '1' => 1
    // ...
    // '9' => 9
    // 'A' => 10
    // 'B' => 11
    // ...
    // 'Z' => 35
    // invalid => -1
    if (value >= '0' && value <= '9')
        return value - '0';
    if (value >= 'A' && value <= 'Z')
        return value - 'A' + 10;
    return -1;
}

constexpr int sqlstate_as_int(std::string_view chars)
{
    if (chars.size() != 5u)
        return -1;
    int c0 = detail::parse_sqlstate_char(chars[0]);
    int c1 = detail::parse_sqlstate_char(chars[1]);
    int c2 = detail::parse_sqlstate_char(chars[2]);
    int c3 = detail::parse_sqlstate_char(chars[3]);
    int c4 = detail::parse_sqlstate_char(chars[4]);
    if (c0 == -1 || c1 == -1 || c2 == -1 || c3 == -1 || c4 == -1)
        return -1;
    return c0 << (6 * 4) | c1 << (6 * 3) | c2 << (6 * 2) | c3 << (6 * 1) | c4;
}

}  // namespace detail

const std::error_category& get_sqlstate_category();

// Compile-time parsing
consteval int operator""_sqlstate(const char* value, std::size_t len)
{
    int res = detail::sqlstate_as_int({value, len});
    if (res == -1)
    {
        throw std::invalid_argument(
            "Invalid SQLSTATE literal: they should be 5 characters length, and contain only numbers and "
            "uppercase letters"
        );
    }
    return res;
}

// Runtime parsing
inline std::error_code parse_sqlstate(std::string_view from)
{
    return std::error_code(detail::sqlstate_as_int(from), get_sqlstate_category());
}

}  // namespace nativepg

#endif
