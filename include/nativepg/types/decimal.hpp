//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_TYPES_DECIMAL_HPP
#define NATIVEPG_TYPES_DECIMAL_HPP

// This header is opt-in: only include it (or nativepg/detail/field_traits_decimal.hpp) if you need to
// parse PostgreSQL "decimal"/"numeric" values into boost::decimal types. Doing so keeps the (heavier)
// Boost.Decimal headers, and the associated field_traits specializations, out of translation units that
// don't use this feature.

#include <boost/decimal/decimal128_t.hpp>
#include <boost/decimal/decimal32_t.hpp>
#include <boost/decimal/decimal64_t.hpp>
#include <boost/endian/conversion.hpp>
#include <boost/system/error_code.hpp>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
#include <utility>

#include "nativepg/client_errc.hpp"
#include "nativepg/field_view.hpp"

namespace nativepg::types {

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
    requires std::same_as<T, boost::decimal::decimal32_t> || std::same_as<T, boost::decimal::decimal64_t> ||
             std::same_as<T, boost::decimal::decimal128_t>
error_code parse_text_decimal(const field_view& from, T& to)
{
    const std::string_view sv = from.data_str();

    // Note: from_chars is noexcept and parses in place, avoiding the heap allocation that the
    // throwing string constructor would incur on malformed input.
    if (const auto res = boost::decimal::from_chars(sv, to);
        res.ec != std::errc{} || res.ptr != sv.data() + sv.size())
        return client_errc::protocol_value_error;
    return {};
}

template <class T>
    requires std::same_as<T, boost::decimal::decimal32_t> || std::same_as<T, boost::decimal::decimal64_t> ||
             std::same_as<T, boost::decimal::decimal128_t>
error_code parse_binary_decimal(const field_view& from, T& to)
{
    const auto bytes = from.data();

    if (bytes.size() < 8)
    {
        return client_errc::protocol_value_error;
    }

    // 1. Safely load header fields from Big-Endian network data
    const auto ndigits = boost::endian::endian_load<uint16_t, 2, boost::endian::order::big>(bytes.data() + 0);
    const auto weight = boost::endian::endian_load<int16_t, 2, boost::endian::order::big>(bytes.data() + 2);
    const auto sign = boost::endian::endian_load<uint16_t, 2, boost::endian::order::big>(bytes.data() + 4);
    const auto dscale = boost::endian::endian_load<uint16_t, 2, boost::endian::order::big>(bytes.data() + 6);

    // Handle NaN state
    if (sign == 0xC000)
    {
        to = std::numeric_limits<T>::quiet_NaN();
        return {};
    }
    // infinity
    else if (sign == 0xD000)
    {
        to = std::numeric_limits<T>::infinity();
        return {};
    }
    // -infinity
    else if (sign == 0xF000)
    {
        to = -std::numeric_limits<T>::infinity();
        return {};
    }

    // Validate expected payload length (8 bytes header + 2 bytes per digit)
    if (bytes.size() < 8UL + (ndigits * 2))
    {
        return client_errc::protocol_value_error;
    }

    T result{0};
    const T base_10k{10000};

    // 2. Accumulate the Base-10000 digit tokens
    for (uint16_t i = 0; i < ndigits; ++i)
    {
        // Calculate offset for current 16-bit digit chunk
        const std::size_t offset = 8 + (i * 2);
        auto raw_digit = boost::endian::endian_load<uint16_t, 2, boost::endian::order::big>(
            bytes.data() + offset
        );

        // Calculate the place value exponent: 10000 ^ (weight - i)
        T place_value = boost::decimal::pow(base_10k, weight - i);

        result += T(raw_digit) * place_value;
    }

    // 3. Enforce the PostgreSQL target display scale (Truncation / Rounding)
    if (dscale > 0)
    {
        T multiplier = boost::decimal::pow(T(10), dscale);

        // Round to the target decimal scale matching PostgreSQL engine specs
        result = boost::decimal::round(result * multiplier) / multiplier;
    }
    else
    {
        // If dscale is 0, it is a strict integer representation
        result = boost::decimal::round(result);
    }

    // 4. Apply the parsed sign flag
    if (sign == 0x4000)
    {
        result = -result;
    }

    to = std::move(result);

    return {};
}

}  // namespace nativepg::types

// Registers field_is_compatible<T>/field_parse<T> for boost::decimal types. Included here (rather than
// force-included by field_traits.hpp) so that only TUs opting into this header pay for it.
#include "nativepg/detail/field_traits_decimal.hpp"

#endif  // NATIVEPG_TYPES_DECIMAL_HPP
