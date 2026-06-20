#ifndef NATIVEPG_TYPES_BASE_HPP
#define NATIVEPG_TYPES_BASE_HPP

#include <boost/endian/conversion.hpp>
#include <boost/multiprecision/fwd.hpp>
#include <boost/system/error_code.hpp>

#include <charconv>
#include <format>
#include <span>
#include <string_view>
#include <system_error>

#include "nativepg/client_errc.hpp"

namespace nativepg::types {

using boost::system::error_code;


/*
| Type      | Category | OID  | Storage size                                                                |
|-----------|----------|------|-----------------------------------------------------------------------------|
| bool      | base     | 16   | 1 byte                                                                      |
| bytea     | base     | 17   | variable (1 or 4 bytes header + actual binary data)                         |
| int2      | base     | 21   | 2 bytes                                                                     |
| int4      | base     | 23   | 4 bytes                                                                     |
| int8      | base     | 20   | 8 bytes                                                                     |
| float4    | base     | 700  | 4 bytes                                                                     |
| float8    | base     | 701  | 8 bytes                                                                     |
| numeric   | base     | 1700 | variable (numeric header + actual digits packed into 2-byte components)     |
| text      | base     | 25   | variable (1 or 4 bytes header + actual string data)                         |
| varchar   | base     | 1043 | variable (1 or 4 bytes header + actual string data)                         |
*/

// Type mapping
using pg_bool = bool;
using pg_bytea = std::vector<std::byte>;
using pg_int2 = std::int16_t;
using pg_int4 = std::int32_t;
using pg_int8 = std::int64_t;
using pg_float4 = float;
using pg_float8 = double;
using pg_numeric = boost::multiprecision::cpp_dec_float_50;
using pg_text = std::string_view;
using pg_varchar = std::string_view;


namespace detail {

template <typename T>
T parse_int(std::string_view sv) {
    T result = 0;

    // std::from_chars expects pointers to the start and end of the characters
    auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), result);

    if (ec == std::errc()) {
        return result; // Success
    } else if (ec == std::errc::result_out_of_range) {
        // Handle overflow/underflow for 16-bit boundaries (-32768 to 32767)
    } else if (ec == std::errc::invalid_argument) {
        // Handle parsing failures (e.g. text instead of numbers)
    }

    return 0;
}

template <typename T>
T parse_floating_point(std::string_view sv) {
    T result = 0.0;

    // Optional third argument defaults to std::chars_format::general
    auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), result);

    if (ec == std::errc()) {
        return result;
    } else if (ec == std::errc::result_out_of_range) {
        //std::cout << "Error: Value overflows double boundaries.\n";
    } else if (ec == std::errc::invalid_argument) {
        //std::cout << "Error: Not a valid floating point representation.\n";
    }

    return 0;
}

}

//BOOL => pg_bool;
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
    return error_code{};
}

template <class T = pg_bool>
constexpr error_code parse_binary_bool(std::span<const unsigned char> from, T& to)
{
    if (from.size() != 1)
        return client_errc::protocol_value_error;
    to = from[0] != 0;
    return error_code{};
}

//BYTEA => pg_bytea;
template <class T = pg_bytea>
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
    return error_code{};
}

template <class T = pg_bytea>
constexpr error_code parse_binary_bytea(std::span<const unsigned char> from, T& to)
{
    // Binary format is raw bytes — copy directly
    to.clear();
    to.reserve(from.size());
    for (auto byte : from)
        to.push_back(static_cast<std::byte>(byte));
    return error_code{};
}

// INT => pg_int
template <class T>
constexpr error_code parse_text_int(std::span<const unsigned char> from, T& to)
{
    if (from.size() > sizeof(T))
        return client_errc::protocol_value_error;

    std::string_view sv{reinterpret_cast<const char*>(from.data()), from.size()};
    to = detail::parse_int<T>(sv);
    return {};
}

template <class T>
constexpr error_code parse_binary_int(std::span<const unsigned char> from, T& to)
{
    if (from.size() != sizeof(T))
        return client_errc::protocol_value_error;
    to = boost::endian::endian_load<T, sizeof(T), boost::endian::order::big>(from.data());
    return {};
}


// FLOAT4 => pg_float4
template <class T = pg_float4>
constexpr error_code parse_text_float4(std::span<const unsigned char> from, T& to)
{
    if (from.size() > sizeof(T))
        return client_errc::protocol_value_error;

    std::string_view sv{reinterpret_cast<const char*>(from.data()), from.size()};
    to = detail::parse_floating_point<T>(sv);
    return {};
}

template <class T = pg_float4>
constexpr error_code parse_binary_float4(std::span<const unsigned char> from, T& to)
{
    if (from.size() != sizeof(T))
        return client_errc::protocol_value_error;

    to = boost::endian::endian_load<T, sizeof(T), boost::endian::order::big>(from.data());
    return {};
}

// FLOAT8 => pg_float8
template <class T = pg_float8>
constexpr error_code parse_text_float8(std::span<const unsigned char> from, T& to)
{
    if (from.size() > sizeof(T))
        return client_errc::protocol_value_error;

    std::string_view sv{reinterpret_cast<const char*>(from.data()), from.size()};

    to = detail::parse_floating_point<T>(sv);
    return {};
}

template <class T = pg_float8>
constexpr error_code parse_binary_float8(std::span<const unsigned char> from, T& to)
{
    if (from.size() != sizeof(T))
        return client_errc::protocol_value_error;

    to = boost::endian::endian_load<T, sizeof(T), boost::endian::order::big>(from.data());
    return {};
}

// NUMERIC => pg_numeric
template <class T = pg_numeric>
constexpr error_code parse_text_numeric(std::span<const unsigned char> from, T& to)
{
    std::string_view sv{reinterpret_cast<const char*>(from.data()), from.size()};
    // Boost.Multiprecision accepts decimal strings including "NaN"
    try {
        to = T{std::string(sv)};
    } catch (...) {
        return client_errc::protocol_value_error;
    }
    return {};
}

template <class T = pg_numeric>
constexpr error_code parse_binary_numeric(std::span<const unsigned char> from, T& to)
{
    // PostgreSQL binary numeric wire format:
    //   int16  ndigits  -- number of base-10000 groups
    //   int16  weight   -- index of the most-significant group (0 = units group)
    //   int16  sign     -- 0x0000=positive, 0x4000=negative, 0xC000=NaN
    //   int16  dscale   -- display scale (ignored here)
    //   int16  digits[] -- ndigits groups, each 0..9999, big-endian

    constexpr std::size_t header_size = 4 * sizeof(std::int16_t);
    if (from.size() < header_size)
        return client_errc::protocol_value_error;

    auto load16 = [&](std::size_t offset) -> std::int16_t {
        return boost::endian::endian_load<std::int16_t, 2, boost::endian::order::big>(from.data() + offset);
    };

    const std::int16_t ndigits = load16(0);
    const std::int16_t weight  = load16(2);
    const std::int16_t sign    = load16(4);
    // dscale at offset 6 is not needed for value reconstruction

    constexpr std::int16_t sign_positive = 0x0000;
    constexpr std::int16_t sign_negative = 0x4000;
    constexpr std::int16_t sign_nan      = static_cast<std::int16_t>(0xC000);

    if (sign == sign_nan)
    {
        to = T{"NaN"};
        return {};
    }
    if (sign != sign_positive && sign != sign_negative)
        return client_errc::protocol_value_error;
    if (ndigits < 0)
        return client_errc::protocol_value_error;
    if (from.size() < header_size + static_cast<std::size_t>(ndigits) * 2)
        return client_errc::protocol_value_error;

    // Reconstruct the value as a string to feed into Boost.Multiprecision.
    // Each base-10000 digit group contributes exactly 4 decimal digits.
    // weight is the power of 10000 for the first (most significant) group:
    //   group[0] = value * 10000^weight
    std::string s;
    s.reserve(ndigits * 4 + 4);

    if (sign == sign_negative)
        s += '-';

    for (std::int16_t i = 0; i < ndigits; ++i)
    {
        uint16_t group = static_cast<std::uint16_t>(
            boost::endian::endian_load<std::int16_t, 2, boost::endian::order::big>(
                from.data() + header_size + i * 2));

        // Determine if this group straddles the decimal point
        // weight==0 means group[0] is the units (10000^0) group.
        // The decimal point sits between weight and weight+1 (in group indices).
        std::int16_t pos = weight - i;  // power-of-10000 for this group

        if (i == 0)
        {
            // First group: write without leading zeros
            s += std::to_string(group);
        }
        else
        {
            // Subsequent groups: always pad to 4 digits
            char buf[5];
            std::snprintf(buf, sizeof(buf), "%04u", static_cast<unsigned>(group));
            s += buf;
        }

        // Insert decimal point after the units group (pos==0)
        if (pos == 0 && i < ndigits - 1)
            s += '.';
    }

    // If weight >= ndigits, all groups are above the decimal point —
    // append trailing zeros to reach the correct magnitude.
    if (weight >= ndigits)
    {
        for (std::int16_t i = ndigits; i <= weight; ++i)
            s += "0000";
    }

    // If weight < 0, all groups are fractional — prepend "0.000..." prefix.
    if (weight < -1)
    {
        std::string prefix = "0.";
        for (std::int16_t i = -1; i > weight; --i)
            prefix += "0000";
        s = (sign == sign_negative ? "-" : "") + prefix + s.substr(sign == sign_negative ? 1 : 0);
    }
    else if (weight == -1)
    {
        // All groups are purely fractional (e.g. 0.xxxx)
        std::string frac = s.substr(sign == sign_negative ? 1 : 0);
        s = (sign == sign_negative ? "-0." : "0.") + frac;
    }

    try {
        to = T{s};
    } catch (...) {
        return client_errc::protocol_value_error;
    }
    return {};
}

// TEXT => pg_text
template <class T = pg_text>
constexpr error_code parse_text_text(std::span<const unsigned char> from, T& to)
{
    if (from.size() != sizeof(T))
        return client_errc::protocol_value_error;
    to = boost::endian::endian_load<T, sizeof(T), boost::endian::order::big>(from.data());
    return {};
}

template <class T = pg_text>
constexpr error_code parse_binary_text(std::span<const unsigned char> from, T& to)
{
    if (from.size() != sizeof(T))
        return client_errc::protocol_value_error;
    to = boost::endian::endian_load<T, sizeof(T), boost::endian::order::big>(from.data());
    return {};
}

// VARCHAR => pg_varchar
template <class T = pg_varchar>
constexpr error_code parse_text_varchar(std::span<const unsigned char> from, T& to)
{
    if (from.size() != sizeof(T))
        return client_errc::protocol_value_error;
    to = boost::endian::endian_load<T, sizeof(T), boost::endian::order::big>(from.data());
    return {};
}

template <class T = pg_varchar>
constexpr error_code parse_binary_varchar(std::span<const unsigned char> from, T& to)
{
    if (from.size() != sizeof(T))
        return client_errc::protocol_value_error;
    to = boost::endian::endian_load<T, sizeof(T), boost::endian::order::big>(from.data());
    return {};
}



}

template <class T>
inline std::basic_ostream<T>& operator<<(std::basic_ostream<T>& os, const nativepg::types::pg_bytea& bytes)
{
    os << "0x";
    for (auto byte : bytes)
        os << std::format("{:02x}", static_cast<int>(byte));
    return os;
}

#endif  // NATIVEPG_TYPES_BASE_HPP
