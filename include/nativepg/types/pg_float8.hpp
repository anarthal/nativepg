//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_TYPES_PG_FLOAT8_HPP
#define NATIVEPG_TYPES_PG_FLOAT8_HPP

#include <bit>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <stdexcept>
#include <boost/core/span.hpp>
#include <boost/system/error_code.hpp>
#include <boost/endian/conversion.hpp>
#include <boost/charconv.hpp>

#include "nativepg/types/pg_type_traits.hpp"

namespace nativepg {
namespace types {

template <>
struct pg_type_traits<double, pg_oid_type::float8>
{
    using value_type = double;

    static constexpr pg_oid_type   oid_type  = pg_oid_type::float8;
    static constexpr std::int32_t  byte_len  = 8;

    static constexpr std::string_view oid_name  = "FLOAT8";
    static constexpr std::string_view type_name = "double";

    static constexpr bool supports_binary = true;

    static boost::system::error_code parse_binary(boost::span<const std::byte> bytes, value_type& val)
    {
        using boost::system::errc::invalid_argument;
        using boost::system::errc::make_error_code;

        // FLOAT8 must be exactly 8 bytes
        if (bytes.size() != byte_len)
            return make_error_code(invalid_argument);

        // Read raw network bytes (big-endian) into a 64-bit unsigned integer
        const std::uint8_t b0 = static_cast<std::uint8_t>(bytes[0]);
        const std::uint8_t b1 = static_cast<std::uint8_t>(bytes[1]);
        const std::uint8_t b2 = static_cast<std::uint8_t>(bytes[2]);
        const std::uint8_t b3 = static_cast<std::uint8_t>(bytes[3]);
        const std::uint8_t b4 = static_cast<std::uint8_t>(bytes[4]);
        const std::uint8_t b5 = static_cast<std::uint8_t>(bytes[5]);
        const std::uint8_t b6 = static_cast<std::uint8_t>(bytes[6]);
        const std::uint8_t b7 = static_cast<std::uint8_t>(bytes[7]);

        std::uint64_t network_value =
            (static_cast<std::uint64_t>(b0) << 56) |
            (static_cast<std::uint64_t>(b1) << 48) |
            (static_cast<std::uint64_t>(b2) << 40) |
            (static_cast<std::uint64_t>(b3) << 32) |
            (static_cast<std::uint64_t>(b4) << 24) |
            (static_cast<std::uint64_t>(b5) << 16) |
            (static_cast<std::uint64_t>(b6) << 8)  |
             static_cast<std::uint64_t>(b7);

        // Convert from big-endian (network) to native endianness using Boost.Endian
        std::uint64_t host_value = boost::endian::big_to_native(network_value);

        // Interpret bits as IEEE-754 double
        val = std::bit_cast<value_type>(host_value);

        return {}; // success
    }

    static boost::system::error_code parse_text(std::string const& str, value_type& val)
    {
        using boost::system::errc::invalid_argument;
        using boost::system::errc::result_out_of_range;
        using boost::system::errc::make_error_code;

        std::string_view text(str);

        // Empty input is invalid
        if (text.empty())
            return make_error_code(invalid_argument);

        value_type parsed = 0.0;
        const char* first = text.data();
        const char* last  = text.data() + text.size();

        auto res = boost::charconv::from_chars(first, last, parsed);

        // from_chars failed
        if (res.ec != std::errc())
        {
            if (res.ec == std::errc::result_out_of_range)
                return make_error_code(result_out_of_range);

            return make_error_code(invalid_argument);
        }

        // Ensure we consumed all characters (no trailing garbage)
        if (res.ptr != last)
            return make_error_code(invalid_argument);

        val = parsed;
        return {}; // success
    }


    static boost::system::error_code serialize_binary(value_type const& val, boost::span<std::byte>& bytes)
    {
        using boost::system::errc::invalid_argument;
        using boost::system::errc::make_error_code;

        // FLOAT8 must be exactly 8 bytes for serialization
        if (bytes.size() != byte_len)
            return make_error_code(invalid_argument);

        // Bit-cast the double to a 64-bit unsigned integer
        std::uint64_t host_value = std::bit_cast<std::uint64_t>(val);

        // Convert from native endianness to big-endian (network order)
        std::uint64_t network_value = boost::endian::native_to_big(host_value);

        // Store as eight big-endian bytes
        bytes[0] = static_cast<std::byte>((network_value >> 56) & 0xFF);
        bytes[1] = static_cast<std::byte>((network_value >> 48) & 0xFF);
        bytes[2] = static_cast<std::byte>((network_value >> 40) & 0xFF);
        bytes[3] = static_cast<std::byte>((network_value >> 32) & 0xFF);
        bytes[4] = static_cast<std::byte>((network_value >> 24) & 0xFF);
        bytes[5] = static_cast<std::byte>((network_value >> 16) & 0xFF);
        bytes[6] = static_cast<std::byte>((network_value >> 8)  & 0xFF);
        bytes[7] = static_cast<std::byte>(network_value & 0xFF);

        return {}; // success
    }

    static boost::system::error_code serialize_text(value_type const& val, std::string& text)
    {
        // Text representation is the decimal string form of the value
        text = std::to_string(val);
        return {}; // success
    }
};


} // namespace types
} // namespace nativepg

#endif // NATIVEPG_TYPES_PG_FLOAT8_HPP