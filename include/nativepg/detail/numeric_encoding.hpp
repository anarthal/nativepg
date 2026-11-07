//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_DETAIL_NUMERIC_ENCODING_HPP
#define NATIVEPG_DETAIL_NUMERIC_ENCODING_HPP

#include <boost/endian/conversion.hpp>

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <system_error>
#include <vector>

#include "nativepg/client_errc.hpp"

namespace nativepg::detail {

inline constexpr std::uint16_t numeric_sign_positive = 0x0000;
inline constexpr std::uint16_t numeric_sign_negative = 0x4000;
inline constexpr std::uint16_t numeric_sign_nan = 0xC000;
inline constexpr std::uint16_t numeric_sign_pos_inf = 0xD000;
inline constexpr std::uint16_t numeric_sign_neg_inf = 0xF000;

// Limits imposed by the numeric binary format
inline constexpr std::int64_t numeric_max_dscale = 0x3FFF;
inline constexpr std::int64_t numeric_max_ndigits = 0xFFFF;

enum class numeric_special
{
    nan,
    pos_inf,
    neg_inf,
};

inline void append_numeric_header(
    std::vector<unsigned char>& to,
    std::uint16_t ndigits,
    std::int16_t weight,
    std::uint16_t sign,
    std::uint16_t dscale
)
{
    const auto offset = to.size();
    to.resize(offset + 8);
    auto* p = to.data() + offset;
    boost::endian::endian_store<std::uint16_t, 2, boost::endian::order::big>(p, ndigits);
    boost::endian::endian_store<std::int16_t, 2, boost::endian::order::big>(p + 2, weight);
    boost::endian::endian_store<std::uint16_t, 2, boost::endian::order::big>(p + 4, sign);
    boost::endian::endian_store<std::uint16_t, 2, boost::endian::order::big>(p + 6, dscale);
}

inline std::error_code serialize_binary_numeric_special(numeric_special kind, std::vector<unsigned char>& to)
{
    const auto sign = kind == numeric_special::nan       ? numeric_sign_nan
                      : kind == numeric_special::pos_inf ? numeric_sign_pos_inf
                                                         : numeric_sign_neg_inf;
    append_numeric_header(to, 0, 0, sign, 0);
    return {};
}

inline void serialize_text_numeric_special(numeric_special kind, std::vector<unsigned char>& to)
{
    const std::string_view sv = kind == numeric_special::nan       ? "NaN"
                                : kind == numeric_special::pos_inf ? "Infinity"
                                                                   : "-Infinity";
    to.insert(to.end(), sv.begin(), sv.end());
}

// Encodes a finite decimal number, given as text, in PostgreSQL's binary numeric format.
// Accepted syntax: [+-]digits[.digits][(e|E)[+-]digits]. Doesn't allocate.
inline std::error_code serialize_binary_numeric_digits(std::string_view sv, std::vector<unsigned char>& to)
{
    // Sign
    bool negative = false;
    if (!sv.empty() && (sv.front() == '+' || sv.front() == '-'))
    {
        negative = sv.front() == '-';
        sv.remove_prefix(1);
    }

    // Integer and fractional digits
    std::size_t pos = 0;
    while (pos < sv.size() && sv[pos] >= '0' && sv[pos] <= '9')
        ++pos;
    std::string_view int_digits = sv.substr(0, pos);
    std::string_view frac_digits;
    if (pos < sv.size() && sv[pos] == '.')
    {
        const std::size_t frac_start = ++pos;
        while (pos < sv.size() && sv[pos] >= '0' && sv[pos] <= '9')
            ++pos;
        frac_digits = sv.substr(frac_start, pos - frac_start);
    }
    if (int_digits.empty() && frac_digits.empty())
        return client_errc::protocol_value_error;

    // Exponent
    std::int64_t exp10 = 0;
    if (pos < sv.size() && (sv[pos] == 'e' || sv[pos] == 'E'))
    {
        ++pos;
        if (pos < sv.size() && sv[pos] == '+')
            ++pos;
        const char* first = sv.data() + pos;
        const char* last = sv.data() + sv.size();
        const auto res = std::from_chars(first, last, exp10);
        if (res.ec != std::errc{})
            return client_errc::protocol_value_error;
        pos = static_cast<std::size_t>(res.ptr - sv.data());
    }
    if (pos != sv.size())
        return client_errc::protocol_value_error;

    // The digits are the concatenation of int_digits and frac_digits
    const std::size_t total = int_digits.size() + frac_digits.size();
    const auto digit_at = [&](std::size_t i) -> int {
        return i < int_digits.size() ? int_digits[i] - '0' : frac_digits[i - int_digits.size()] - '0';
    };

    // Number of digits that precede the decimal point, and the display scale
    std::int64_t point = static_cast<std::int64_t>(int_digits.size()) + exp10;
    std::int64_t dscale = static_cast<std::int64_t>(frac_digits.size()) - exp10;
    if (dscale < 0)
        dscale = 0;
    if (dscale > numeric_max_dscale)
        return client_errc::value_too_big;

    // Skip leading and trailing zeros
    std::size_t begin = 0;
    while (begin < total && digit_at(begin) == 0)
        ++begin;
    if (begin == total)
    {
        // Zero is encoded with no digit groups
        append_numeric_header(to, 0, 0, numeric_sign_positive, static_cast<std::uint16_t>(dscale));
        return {};
    }
    std::size_t end = total;
    while (digit_at(end - 1) == 0)
        --end;
    point -= static_cast<std::int64_t>(begin);

    // Groups of 4 decimal digits, aligned to the decimal point. The weight is floor((point - 1) / 4)
    std::int64_t weight = (point - 1) / 4;
    if ((point - 1) % 4 < 0)
        --weight;
    if (weight > 0x7FFF || weight < -0x8000)
        return client_errc::value_too_big;

    // Number of zeros that must be prepended to fill the first group
    const std::int64_t pad = 4 - (point - 4 * weight);
    const std::int64_t num_digits = static_cast<std::int64_t>(end - begin);
    const std::int64_t ndigits = (pad + num_digits + 3) / 4;
    if (ndigits > numeric_max_ndigits)
        return client_errc::value_too_big;

    append_numeric_header(
        to,
        static_cast<std::uint16_t>(ndigits),
        static_cast<std::int16_t>(weight),
        negative ? numeric_sign_negative : numeric_sign_positive,
        static_cast<std::uint16_t>(dscale)
    );

    const auto offset = to.size();
    to.resize(offset + static_cast<std::size_t>(ndigits) * 2);
    for (std::int64_t g = 0; g < ndigits; ++g)
    {
        std::uint16_t group = 0;
        for (std::int64_t k = 0; k < 4; ++k)
        {
            const std::int64_t idx = g * 4 + k - pad;
            const int digit = idx >= 0 && idx < num_digits ? digit_at(begin + static_cast<std::size_t>(idx))
                                                           : 0;
            group = static_cast<std::uint16_t>(group * 10 + digit);
        }
        boost::endian::endian_store<std::uint16_t, 2, boost::endian::order::big>(
            to.data() + offset + static_cast<std::size_t>(g) * 2,
            group
        );
    }
    return {};
}

}  // namespace nativepg::detail

#endif
