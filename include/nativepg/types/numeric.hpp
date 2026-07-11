//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_TYPES_NUMERIC_HPP
#define NATIVEPG_TYPES_NUMERIC_HPP

#ifdef NATIVEPG_USE_NUMERIC_TYPES

#include <boost/endian/conversion.hpp>
#include <boost/multiprecision/cpp_dec_float.hpp>
#include <boost/multiprecision/number.hpp>
#include <boost/system/error_code.hpp>

#include <span>
#include <string_view>
#include <limits>
#include <cstdint>
#include <cstddef>


#include "nativepg/client_errc.hpp"



namespace nativepg::types {

using boost::system::error_code;
namespace mp = boost::multiprecision;


/*
 Type mapping
| Type      | Category | OID  | C++ type                                | Storage size                                                                |
|-----------|----------|------|-----------------------------------------|-----------------------------------------------------------------------------|
| numeric   | base     | 1700 | boost::multiprecision::number<cpp_dec_float<DIGITS>> Variable (numeric header + actual digits packed into 2-byte components)     |
*/

namespace detail {

// Counts significant decimal digits of a Postgres numeric text value.
// Ignores sign, decimal point, leading zeros, and trailing zeros.
// Non-digit tails (NaN / Infinity / exponent) stop the scan.
inline std::size_t count_significant_digits(std::string_view s) noexcept
{
    std::size_t i = 0;
    if (i < s.size() && (s[i] == '+' || s[i] == '-'))
        ++i;

    std::size_t sig = 0;             // significant digits confirmed
    std::size_t pending_zeros = 0;   // zeros after first non-zero, not yet confirmed
    bool seen_nonzero = false;

    for (; i < s.size(); ++i)
    {
        const char c = s[i];
        if (c == '.')
            continue;                // scale marker, not a digit
        if (c < '0' || c > '9')
            break;                   // 'e'/'E', end of NaN/Inf, etc.

        if (c == '0')
        {
            if (seen_nonzero)
                ++pending_zeros;     // could be interior; decide when we see next non-zero
            // leading zeros: ignored
        }
        else
        {
            seen_nonzero = true;
            sig += pending_zeros + 1; // absorb the interior zeros
            pending_zeros = 0;
        }
    }
    return sig;                      // trailing zeros intentionally dropped
}

// Counts significant decimal digits carried by a Postgres numeric binary
// payload's base-10000 digit groups (most significant first). Consistent with
// count_significant_digits: leading and trailing zeros are excluded.
// `groups` must point at the first group, with at least `ndigits` groups readable.
inline std::size_t count_significant_digits_binary(
    std::uint16_t ndigits,
    const unsigned char* groups) noexcept
{
    if (ndigits == 0)
        return 0;

    const auto load = [](const unsigned char* p) {
        return boost::endian::endian_load<std::uint16_t, 2, boost::endian::order::big>(p);
    };

    // The most significant group is rendered without leading zeros.
    const std::uint16_t first = load(groups);
    const std::size_t lead_width = first >= 1000 ? 4 : first >= 100 ? 3 : first >= 10 ? 2 : 1;

    const std::size_t total = lead_width + static_cast<std::size_t>(ndigits - 1) * 4;

    // Drop trailing zeros, walking up from the least significant group.
    std::size_t trailing = 0;
    for (int i = static_cast<int>(ndigits) - 1; i >= 0; --i)
    {
        std::uint16_t g = load(groups + i * 2);
        const std::size_t width = (i == 0) ? lead_width : 4;
        if (g == 0)
        {
            trailing += width;   // whole group is zero
            continue;
        }
        std::size_t z = 0;
        while (z < width && (g % 10) == 0)
        {
            g /= 10;
            ++z;
        }
        trailing += z;
        break;
    }
    return total - trailing;
}

};


template <class T>
inline error_code parse_text_numeric(const std::span<const unsigned char> from, T& to)
{
    std::string_view sv{reinterpret_cast<const char*>(from.data()), from.size()};

    constexpr auto type_digits = static_cast<std::size_t>(std::numeric_limits<T>::digits10);
    if (detail::count_significant_digits(sv) > type_digits)
        return client_errc::incompatible_response_length;

    try {
        // Boost.Multiprecision accepts decimal strings including "NaN"
        to = T(sv);
    } catch (...) {
        return client_errc::protocol_value_error;
    }
    return {};
}

template <class T>
inline error_code parse_binary_numeric(const std::span<const unsigned char> from, T& to)
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

    // Reject values whose significant digits exceed the target precision.
    // Groups are guaranteed in-bounds by the length check above.
    if (constexpr auto type_digits = static_cast<std::size_t>(std::numeric_limits<T>::digits10);
        detail::count_significant_digits_binary(ndigits, from.data() + 8) > type_digits)
        return client_errc::incompatible_response_length;

    T result = 0;
    const T base_10k = 10000;

    // 2. Accumulate the Base-10000 digit tokens
    for (uint16_t i = 0; i < ndigits; ++i) {
        // Calculate offset for current 16-bit digit chunk
        const std::size_t offset = 8 + (i * 2);
        auto raw_digit = boost::endian::endian_load<uint16_t, 2, boost::endian::order::big>(from.data() + offset);

        // Calculate the place value exponent: 10000 ^ (weight - i)
        T place_value = mp::pow(base_10k, weight - i);

        result += T(raw_digit) * place_value;
    }

    // 3. Enforce the PostgreSQL target display scale (Truncation / Rounding)
    if (dscale > 0)
    {
        T multiplier = mp::pow(T(10), dscale);

        // Round to the target decimal scale matching PostgreSQL engine specs
        result = mp::round(result * multiplier) / multiplier;
    }
    else
    {
        // If dscale is 0, it is a strict integer representation
        result = mp::round(result);
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

#endif  // NATIVEPG_TYPES_NUMERIC_HPP
