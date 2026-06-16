//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_SRC_SQLSTATE_TO_STRING_HPP
#define NATIVEPG_SRC_SQLSTATE_TO_STRING_HPP

#include <boost/assert.hpp>

#include <string>

// Exposed for testing

namespace nativepg::detail {

// Inverse of parse_sqlstate_char
constexpr char unparse_sqlstate_char(int value)
{
    //   0..9  => '0'..'9'
    //   10..35 => 'A'..'Z'
    BOOST_ASSERT(value >= 0 && value <= 35);
    return value < 10 ? static_cast<char>('0' + value) : static_cast<char>('A' + value - 10);
}

// Inverse of sqlstate_as_int: unpacks the five 6-bit base-36 digits back into a 5-char string.
inline std::string unknown_sqlstate_to_string(int val)
{
    // As a safety measure, discard everything except the 30 least significant bits
    // that encode our value
    val &= 0x3fffffff;

    // Parse in groups of 6 bits
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
