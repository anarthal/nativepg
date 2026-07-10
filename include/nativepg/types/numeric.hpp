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


namespace nativepg {
namespace types {

using boost::system::error_code;
namespace mp = boost::multiprecision;


/*
 Type mapping
| Type      | Category | OID  | C++ type                                | Storage size                                                                |
|-----------|----------|------|-----------------------------------------|-----------------------------------------------------------------------------|
| numeric   | base     | 1700 | boost::multiprecision::number<cpp_dec_float<DIGITS>> Variable (numeric header + actual digits packed into 2-byte components)     |
*/

namespace detail {

}

template <std::size_t TDigits, class T = mp::number<boost::multiprecision::cpp_dec_float<TDigits>>>
inline error_code parse_text_numeric(std::span<const unsigned char> from, T& to)
{
    std::string_view sv{reinterpret_cast<const char*>(from.data()), from.size()};
    // Boost.Multiprecision accepts decimal strings including "NaN"
    try {
        to = T(sv);
    } catch (...) {
        return client_errc::protocol_value_error;
    }
    return {};
}

template <std::size_t TDigits, class T = mp::number<boost::multiprecision::cpp_dec_float<TDigits>>>
inline error_code parse_binary_numeric(std::span<const unsigned char> from, T& to)
{
    if (from.size() < 8) {
        return client_errc::protocol_value_error;
    }

    // 1. Safely load header fields from Big-Endian network data
    auto ndigits = boost::endian::endian_load<uint16_t, 2, boost::endian::order::big>(from.data() + 0);
    auto weight  = boost::endian::endian_load<int16_t,  2, boost::endian::order::big>(from.data() + 2);
    auto sign    = boost::endian::endian_load<uint16_t, 2, boost::endian::order::big>(from.data() + 4);
    auto dscale  = boost::endian::endian_load<uint16_t, 2, boost::endian::order::big>(from.data() + 6);

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

    T result = 0;
    const T base_10k = 10000;

    // 2. Accumulate the Base-10000 digit tokens
    for (uint16_t i = 0; i < ndigits; ++i) {
        // Calculate offset for current 16-bit digit chunk
        std::size_t offset = 8 + (i * 2);
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
}

#endif

#endif  // NATIVEPG_TYPES_NUMERIC_HPP
