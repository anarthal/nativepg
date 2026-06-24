#ifndef NATIVEPG_TYPES_BASE_HPP
#define NATIVEPG_TYPES_BASE_HPP

#include <boost/endian/conversion.hpp>
#include <boost/multiprecision/cpp_dec_float.hpp>
#include <boost/system/error_code.hpp>

#include <charconv>
#include <format>
#include <span>
#include <string_view>
#include <system_error>
#include <vector>
#include <limits>
#include <cstdint>
#include <cstddef>


#include "nativepg/client_errc.hpp"

namespace nativepg {
namespace types {

using boost::system::error_code;


/*
 Type mapping
| Type      | Category | OID  | C++ type                                | Storage size                                                                |
|-----------|----------|------|-----------------------------------------|-----------------------------------------------------------------------------|
| bool      | base     | 16   | bool                                    | 1 byte                                                                      |
| bytea     | base     | 17   | std::vector<std::byte>>                 | variable (1 or 4 bytes header + actual binary data)                         |
| int2      | base     | 21   | std::int16_t                            | 2 bytes                                                                     |
| int4      | base     | 23   | std::int32_t                            | 4 bytes                                                                     |
| int8      | base     | 20   | std::int64_t                            | 8 bytes                                                                     |
| float4    | base     | 700  | float                                   | 4 bytes                                                                     |
| float8    | base     | 701  | double                                  | 8 bytes                                                                     |
| numeric   | base     | 1700 | boost::multiprecision::cpp_dec_float_100 | variable (numeric header + actual digits packed into 2-byte components)     |
| text      | base     | 25   | std::string_view                        | variable (1 or 4 bytes header + actual string data)                         |
| varchar   | base     | 1043 | std::string_view                        | variable (1 or 4 bytes header + actual string data)                         |
*/

constexpr std::size_t max_digits_text = 25;

namespace detail {

template <typename T>
constexpr error_code parse_text_to_number(std::string_view sv, T& to) {
    T result = 0;
    auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), result);
    if (ec == std::errc{} && ptr == sv.data() + sv.size())
        to = std::move(result); // Success
    else
        return client_errc::protocol_value_error;

    return {};
}

}


//BOOL => bool;
template <class T = bool>
constexpr  error_code parse_text_bool(std::span<const unsigned char> from, T& to)
{
    std::string_view sv{reinterpret_cast<const char*>(from.data()), from.size()};
    if (sv == "t" || sv == "true")
        to = true;
    else if (sv == "f" || sv == "false")
        to = false;
    else
        return client_errc::protocol_value_error;
    return {};
}

template <class T = bool>
constexpr error_code parse_binary_bool(std::span<const unsigned char> from, T& to)
{
    if (from.size() != 1)
        return client_errc::protocol_value_error;
    to = from[0] != 0;
    return {};
}

//BYTEA => std::vector<std::byte>>;
template <class T = std::vector<std::byte>>
constexpr error_code parse_text_bytea(std::span<const unsigned char> from, T& to)
{
    // PostgreSQL text format for bytea is \x followed by hex pairs
    std::string_view sv{reinterpret_cast<const char*>(from.data()), from.size()};
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
        auto res = std::from_chars(sv.data() + i, sv.data() + i + 2, byte, 16);
        if (res.ec != std::errc{} || res.ptr != sv.data() + i + 2)
            return client_errc::protocol_value_error;
        to.push_back(static_cast<std::byte>(byte));
    }
    return {};
}

template <class T = std::vector<std::byte>>
constexpr error_code parse_binary_bytea(std::span<const unsigned char> from, T& to)
{
    // Binary format is raw bytes — copy directly
    to.clear();
    to.reserve(from.size());
    for (auto byte : from)
        to.push_back(static_cast<std::byte>(byte));
    return {};
}

// INT => std::int_t
template <class T>
constexpr error_code parse_text_int(std::span<const unsigned char> from, T& to)
{
    if (from.size() > max_digits_text)
        return client_errc::protocol_value_error;

    std::string_view sv{reinterpret_cast<const char*>(from.data()), from.size()};
    return detail::parse_text_to_number<T>(sv, to);
}

template <class T>
constexpr error_code parse_binary_int(std::span<const unsigned char> from, T& to)
{
    if (from.size() != sizeof(T))
        return client_errc::protocol_value_error;
    to = boost::endian::endian_load<T, sizeof(T), boost::endian::order::big>(from.data());
    return {};
}


// FLOAT => float
template <class T>
constexpr error_code parse_text_float(std::span<const unsigned char> from, T& to)
{
    if (from.size() > max_digits_text)
        return client_errc::protocol_value_error;

    std::string_view sv{reinterpret_cast<const char*>(from.data()), from.size()};
    return detail::parse_text_to_number<T>(sv, to);
}

template <class T>
constexpr error_code parse_binary_float(std::span<const unsigned char> from, T& to)
{
    if (from.size() != sizeof(T))
        return client_errc::protocol_value_error;

    to = boost::endian::endian_load<T, sizeof(T), boost::endian::order::big>(from.data());
    return {};
}


// NUMERIC => boost::multiprecision::cpp_dec_float_50 or cpp_dec_float_100
// NOTE: Both numeric parse methods are inline because Boost.Multiprecision can't work constexpr.
template <class T>
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

template <class T>
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
        T place_value = boost::multiprecision::pow(base_10k, weight - i);

        result += T(raw_digit) * place_value;
    }

    // 3. Enforce the PostgreSQL target display scale (Truncation / Rounding)
    if (dscale > 0)
    {
        T multiplier = boost::multiprecision::pow(T(10), dscale);

        // Round to the target decimal scale matching PostgreSQL engine specs
        result = boost::multiprecision::round(result * multiplier) / multiplier;
    }
    else
    {
        // If dscale is 0, it is a strict integer representation
        result = boost::multiprecision::round(result);
    }

    // 4. Apply the parsed sign flag
    if (sign == 0x4000) {
        result = -result;
    }

    to = std::move(result);

    return {};
}


// TEXT | VARCHAR => std::string_view or std::string
template <class T>
constexpr error_code parse_text_text(std::span<const unsigned char> from, T& to)
{
    to = T{reinterpret_cast<const char*>(from.data()), from.size()};
    return {};
}

template <class T>
constexpr error_code parse_binary_text(std::span<const unsigned char> from, T& to)
{
    to = T{reinterpret_cast<const char*>(from.data()), from.size()};
    return {};
}

}

template <class T>
inline std::basic_ostream<T>& operator<<(std::basic_ostream<T>& os, const std::vector<std::byte>& bytes)
{
    os << "0x";
    for (auto byte : bytes)
        os << std::format("{:02x}", static_cast<int>(byte));
    return os;
}

}

#endif  // NATIVEPG_TYPES_BASE_HPP
