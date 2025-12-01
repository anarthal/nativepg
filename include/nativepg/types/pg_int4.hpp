//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_TYPES_PG_INT4_HPP
#define NATIVEPG_TYPES_PG_INT4_HPP

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <boost/charconv.hpp>
#include <boost/endian/conversion.hpp>
#include <boost/core/span.hpp>
#include <boost/system/error_code.hpp>

#include "nativepg/types/pg_type_traits.hpp"


namespace nativepg {
namespace types {


template <>
struct pg_type_traits<std::int32_t, pg_oid_type::int4>
{
    using value_type = std::int32_t;

    static constexpr pg_oid_type oid_type = pg_oid_type::int4;

    static constexpr std::string_view oid_name  = "INT4";
    static constexpr std::string_view type_name = "std::int32_t";

    static constexpr std::int32_t byte_len = 4;
    static constexpr bool supports_binary = true;

    static boost::system::error_code parse_binary(boost::span<const std::byte> bytes, value_type& val)
    {
        using boost::system::errc::invalid_argument;
        using boost::system::errc::make_error_code;

        // int4 must be exactly 4 bytes
        if (bytes.size() != byte_len)
            return make_error_code(invalid_argument);

        // Read raw network bytes (big-endian) into a 32-bit unsigned integer
        const std::uint8_t b0 = static_cast<std::uint8_t>(bytes[0]);
        const std::uint8_t b1 = static_cast<std::uint8_t>(bytes[1]);
        const std::uint8_t b2 = static_cast<std::uint8_t>(bytes[2]);
        const std::uint8_t b3 = static_cast<std::uint8_t>(bytes[3]);

        std::uint32_t network_value =
            (static_cast<std::uint32_t>(b0) << 24) |
            (static_cast<std::uint32_t>(b1) << 16) |
            (static_cast<std::uint32_t>(b2) << 8)  |
             static_cast<std::uint32_t>(b3);

        // Convert from big-endian (network) to native endianness using Boost.Endian
        std::uint32_t host_value = boost::endian::big_to_native(network_value);

        // Cast to signed 32-bit (PostgreSQL int4 is signed)
        val = static_cast<value_type>(host_value);

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

        // Parse as decimal using boost::charconv
        std::int32_t parsed = 0;
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

        // Range check for value_type (int32_t)
        if (parsed < std::numeric_limits<value_type>::min() ||
            parsed > std::numeric_limits<value_type>::max())
        {
            return make_error_code(result_out_of_range);
        }

        val = static_cast<value_type>(parsed);
        return {}; // success
    }


    static boost::system::error_code serialize_binary(value_type const& val, boost::span<std::byte>& bytes)
    {
        using boost::system::errc::invalid_argument;
        using boost::system::errc::make_error_code;

        // int4 must be exactly 4 bytes for serialization
        if (bytes.size() != byte_len)
            return make_error_code(invalid_argument);

        // Convert signed 32-bit value to unsigned host representation
        std::uint32_t host_value = static_cast<std::uint32_t>(val);

        // Convert from native endianness to big-endian (network order)
        std::uint32_t network_value = boost::endian::native_to_big(host_value);

        // Store as four big-endian bytes
        bytes[0] = static_cast<std::byte>((network_value >> 24) & 0xFF);
        bytes[1] = static_cast<std::byte>((network_value >> 16) & 0xFF);
        bytes[2] = static_cast<std::byte>((network_value >> 8)  & 0xFF);
        bytes[3] = static_cast<std::byte>(network_value & 0xFF);

        return {}; // success
    }

    static boost::system::error_code serialize_text(value_type const& val, std::string& text)
    {
        // Text representation is the decimal string form of the value
        text = std::to_string(val);
        return {}; // success
    }

};


}
}

#endif
