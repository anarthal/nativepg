//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_TYPES_PG_INT2_HPP
#define NATIVEPG_TYPES_PG_INT2_HPP


#include <cstddef>
#include <limits>
#include <string>
#include <boost/charconv.hpp>

#include <boost/system/system_error.hpp>
#include <boost/endian/conversion.hpp>

#include "nativepg/client_errc.hpp"
#include "nativepg/types/readable_traits.hpp"
#include "nativepg/types/writeable_traits.hpp"
#include "nativepg/types/pg_type_traits.hpp"


namespace nativepg {
namespace types {

// Type Mapping between C++ and PostgreSQL and vice versa.
template <>
struct pg_type_traits<std::int16_t, pg_oid_type::int2>
{
    using value_type = std::int16_t;

    // Meta info
    static constexpr pg_oid_type oid_type = pg_oid_type::int2;
    static constexpr std::int32_t byte_len = 2;

    static constexpr std::string_view oid_name  = "INT2";
    static constexpr std::string_view type_name = "std::int16_t";

    static constexpr bool supports_binary = true;

    // readable_traits methods
    static boost::system::error_code parse_binary(boost::span<const std::byte> const& bytes, value_type& out)
    {
        using boost::system::errc::invalid_argument;
        using boost::system::errc::make_error_code;

        // int16 must be exactly 2 bytes
        if (bytes.size() != byte_len)
            return make_error_code(invalid_argument);

        // Read raw network bytes (big-endian) into a 16-bit unsigned integer
        const std::uint8_t b0 = static_cast<std::uint8_t>(bytes[0]);
        const std::uint8_t b1 = static_cast<std::uint8_t>(bytes[1]);

        std::uint16_t network_value =
            (static_cast<std::uint16_t>(b0) << 8) |
             static_cast<std::uint16_t>(b1);

        // Convert from big-endian (network) to native endianness using Boost.Endian
        std::uint16_t host_value = boost::endian::big_to_native(network_value);

        // Cast to signed 16-bit (PostgreSQL int2 is signed)
        out = static_cast<value_type>(host_value);

        return {}; // success
    }

    static boost::system::error_code parse_text(std::string_view text, value_type& out)
    {
        using boost::system::errc::invalid_argument;
        using boost::system::errc::result_out_of_range;
        using boost::system::errc::make_error_code;

        // Empty input is invalid
        if (text.empty())
            return make_error_code(invalid_argument);

        // Parse as decimal using boost::charconv
        int value32 = 0;
        const char* first = text.data();
        const char* last  = text.data() + text.size();

        auto res = boost::charconv::from_chars(first, last, value32);

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

        // Range check for int16_t
        if (value32 < std::numeric_limits<value_type>::min() ||
            value32 > std::numeric_limits<value_type>::max())
        {
            return make_error_code(result_out_of_range);
        }

        out = static_cast<value_type>(value32);
        return {}; // success
    }

    // writeable_traits methods
    static boost::system::error_code serialize_binary(const value_type& val, boost::span<std::byte>& bytes)
    {
        using boost::system::errc::invalid_argument;
        using boost::system::errc::make_error_code;

        // int16 must be exactly 2 bytes for serialization
        if (bytes.size() != byte_len)
            return make_error_code(invalid_argument);

        // Convert signed 16-bit value to unsigned host representation
        std::uint16_t host_value = static_cast<std::uint16_t>(val);

        // Convert from native endianness to big-endian (network order)
        std::uint16_t network_value = boost::endian::native_to_big(host_value);

        // Store as two big-endian bytes
        bytes[0] = static_cast<std::byte>((network_value >> 8) & 0xFF);
        bytes[1] = static_cast<std::byte>(network_value & 0xFF);

        return {}; // success
    }

    static boost::system::error_code serialize_text(const value_type& val, std::string& text)
    {
        // Text representation is the decimal string form of the value
        text = std::to_string(val);
        return {}; // success
    }
};
static_assert(readable_traits<pg_type_traits<std::int16_t, pg_oid_type::int2>>);
static_assert(writeable_traits<pg_type_traits<std::int16_t, pg_oid_type::int2>>);


}
}

#endif
