//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_SRC_SQLSTATE_TO_STRING_HPP
#define NATIVEPG_SRC_SQLSTATE_TO_STRING_HPP

#include <boost/assert.hpp>

#include <array>

// Exposed for testing

namespace nativepg::detail {

// Inverse of parse_sqlstate_char. Returns \0 if the value is invalid
constexpr char unparse_sqlstate_char(int value)
{
    //   0..9  => '0'..'9'
    //   10..35 => 'A'..'Z'
    if (value < 0)
        return '\0';
    if (value < 10)
        return static_cast<char>('0' + value);
    if (value <= 35)
        return static_cast<char>('A' + value - 10);
    return '\0';
}

// True if value is a code that sqlstate_as_int could have produced: non-negative,
// no bits set above the 30 we use, and every 6-bit group a valid base-36 digit (0..35).
constexpr bool is_valid_sqlstate(int val)
{
    constexpr int six_bits_mask = 0x3f;
    // clang-format off
    return
        val >= 0 &&
        val <= 0x3fffffff &&
        unparse_sqlstate_char(val >> 24) != '\0' &&
        unparse_sqlstate_char((val >> 18) & six_bits_mask) != '\0' &&
        unparse_sqlstate_char((val >> 12) & six_bits_mask) != '\0' &&
        unparse_sqlstate_char((val >> 6) & six_bits_mask) != '\0' &&
        unparse_sqlstate_char(val & six_bits_mask)!= '\0';
    // clang-format on
}

// Inverse of sqlstate_as_int: unpacks the five 6-bit base-36 digits back into a 5-char string.
// Precondition: val should be a valid value
constexpr std::array<char, 5> sqlstate_as_string(int val)
{
    BOOST_ASSERT(is_valid_sqlstate(val));
    constexpr int six_bits_mask = 0x3f;
    return {
        unparse_sqlstate_char(val >> 24),
        unparse_sqlstate_char((val >> 18) & six_bits_mask),
        unparse_sqlstate_char((val >> 12) & six_bits_mask),
        unparse_sqlstate_char((val >> 6) & six_bits_mask),
        unparse_sqlstate_char(val & six_bits_mask),
    };
}

}  // namespace nativepg::detail

#endif
