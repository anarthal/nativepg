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

#include <cstdint>
#include <cstddef>
#include <limits>
#include <ranges>
#include <span>
#include <string_view>

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

// Note: Only needed to guard losing precision. E.g. if postgresql type fits into the Boost C++ type.
inline std::size_t count_significant_digits_text(const std::string_view s) noexcept
{
    std::string_view ws = s;

    if (!ws.empty() && (ws[0] == '+' || ws[0] == '-')) {
        ws.remove_prefix(1);
    }

    const auto non_digit = std::ranges::find_if(ws, [](const char c) {
        return (c < '0' || c > '9') && c != '.';
    });

    std::string_view numeric_part = ws.substr(0, std::distance(ws.begin(), non_digit));

    auto sig_digits_view = numeric_part
                         | std::views::filter([](const char c) { return c != '.'; })
                         | std::views::drop_while([](const char c) { return c == '0'; })
                         | std::views::reverse
                         | std::views::drop_while([](const char c) { return c == '0'; });

    return static_cast<std::size_t>(std::ranges::distance(sig_digits_view));
}

// Note: Only needed to guard losing precision. E.g. if postgresql type fits into the Boost C++ type.
inline std::size_t count_significant_digits_binary(
    const std::uint16_t ndigits,
    const unsigned char* groups) noexcept
{
    if (ndigits == 0)
        return 0;

    const auto load = [groups](const std::size_t i) {
        return boost::endian::endian_load<std::uint16_t, 2, boost::endian::order::big>(groups + i * 2);
    };

    // The most significant group is rendered without leading zeros.
    const std::uint16_t first = load(0);
    const std::size_t lead_width = first >= 1000 ? 4 : first >= 100 ? 3 : first >= 10 ? 2 : 1;
    const std::size_t total = lead_width + static_cast<std::size_t>(ndigits - 1) * 4;

    const auto zero_groups = static_cast<std::size_t>(std::ranges::distance(
        std::views::iota(std::size_t{0}, std::size_t{ndigits})
        | std::views::reverse
        | std::views::take_while([&](const std::size_t i) { return load(i) == 0; })));

    if (zero_groups == ndigits)
        return 0;

    std::size_t trailing = zero_groups * 4;
    for (std::uint16_t g = load(ndigits - 1 - zero_groups); g % 10 == 0; g /= 10)
        ++trailing;

    return total - trailing;
}

};


template <class T>
inline error_code parse_text_numeric(const std::span<const unsigned char> from, T& to)
{
    std::string_view sv{reinterpret_cast<const char*>(from.data()), from.size()};

    constexpr auto type_digits = static_cast<std::size_t>(std::numeric_limits<T>::digits10);
    if (detail::count_significant_digits_text(sv) > type_digits)
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
