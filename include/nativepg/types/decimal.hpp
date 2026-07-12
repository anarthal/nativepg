//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_TYPES_DECIMAL_HPP
#define NATIVEPG_TYPES_DECIMAL_HPP

#ifdef NATIVEPG_USE_DECIMAL_TYPES
#define BOOST_DECIMAL_HAS_STD_STRING_VIEW

#include <boost/endian/conversion.hpp>
#include <boost/decimal.hpp>
#include <boost/system/error_code.hpp>

#include <concepts>
#include <cstdint>
#include <cstddef>
#include <limits>
#include <ranges>
#include <span>
#include <string_view>

#include "nativepg/client_errc.hpp"

namespace nativepg::types {

using boost::system::error_code;
namespace bd = boost::decimal;


/*
 Type mapping
| Type      | Category | OID  | C++ type                      | Storage size  |
|-----------|----------|------|-------------------------------|---------------|
| decimal   | decimal  | 1700 | boost::decimal::decimal32_t   | 32 bits       |
| decimal   | decimal  | 1700 | boost::decimal::decimal64_t   | 64 bits       |
| decimal   | decimal  | 1700 | boost::decimal::decimal128_t  | 128 bits      |
*/

namespace detail {

}


template <class T>
    requires std::same_as<T, bd::decimal32_t> ||
                std::same_as<T, bd::decimal64_t> ||
                std::same_as<T, bd::decimal128_t>
inline error_code parse_text_decimal(const std::span<const unsigned char> from, T& to)
{
    std::string_view sv{reinterpret_cast<const char*>(from.data()), from.size()};

    try {
        to = T{sv};
    } catch (...) {
        return client_errc::protocol_value_error;
    }
    return {};
}

template <class T>
    requires std::same_as<T, bd::decimal32_t> ||
                std::same_as<T, bd::decimal64_t> ||
                std::same_as<T, bd::decimal128_t>
inline error_code parse_binary_decimal(const std::span<const unsigned char> from, T& to)
{
    if (from.size() < 8) {
        return client_errc::protocol_value_error;
    }

    // 1. Safely load header fields from Big-Endian network data
    const auto ndigits = boost::endian::endian_load<uint16_t, 2, boost::endian::order::big>(from.data() + 0);
    const auto weight  = boost::endian::endian_load<int16_t,  2, boost::endian::order::big>(from.data() + 2);
    const auto sign    = boost::endian::endian_load<uint16_t, 2, boost::endian::order::big>(from.data() + 4);
    const auto dscale  = boost::endian::endian_load<uint16_t, 2, boost::endian::order::big>(from.data() + 6);

    // Handle NaN state
    if (sign == 0xC000) {
        to = std::numeric_limits<T>::quiet_NaN();
        return {};
    }
    // infinity
    else if (sign == 0xD000) {
        to = std::numeric_limits<T>::infinity();
        return {};
    }
    // -infinity
    else if (sign == 0xF000) {
        to = -std::numeric_limits<T>::infinity();
        return {};
    }

    // Validate expected payload length (8 bytes header + 2 bytes per digit)
    if (from.size() < 8UL + (ndigits * 2)) {
        return client_errc::protocol_value_error;
    }

    T result{0};
    const T base_10k{10000};

    // 2. Accumulate the Base-10000 digit tokens
    for (uint16_t i = 0; i < ndigits; ++i) {
        // Calculate offset for current 16-bit digit chunk
        const std::size_t offset = 8 + (i * 2);
        auto raw_digit = boost::endian::endian_load<uint16_t, 2, boost::endian::order::big>(from.data() + offset);

        // Calculate the place value exponent: 10000 ^ (weight - i)
        T place_value = bd::pow(base_10k, weight - i);

        result += T(raw_digit) * place_value;
    }

    // 3. Enforce the PostgreSQL target display scale (Truncation / Rounding)
    if (dscale > 0)
    {
        T multiplier = bd::pow(T(10), dscale);

        // Round to the target decimal scale matching PostgreSQL engine specs
        result = bd::round(result * multiplier) / multiplier;
    }
    else
    {
        // If dscale is 0, it is a strict integer representation
        result = bd::round(result);
    }

    // 4. Apply the parsed sign flag
    if (sign == 0x4000) {
        result = -result;
    }

    to = std::move(result);

    return {};
}

}


#endif

#endif  // NATIVEPG_TYPES_DECIMAL_HPP
