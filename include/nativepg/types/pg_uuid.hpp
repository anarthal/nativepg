//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_TYPES_PG_UUID_HPP
#define NATIVEPG_TYPES_PG_UUID_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <algorithm>
#include <boost/core/span.hpp>
#include <boost/system/error_code.hpp>
#include <boost/endian/conversion.hpp>
#include <stdexcept>

#include <boost/lexical_cast.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

#include "nativepg/types/pg_type_traits.hpp"

namespace nativepg {
namespace types {

template <>
struct pg_type_traits<boost::uuids::uuid, pg_oid_type::uuid>
{
    using value_type = boost::uuids::uuid;

    static constexpr pg_oid_type   oid_type  = pg_oid_type::uuid;

    static constexpr std::string_view oid_name  = "UUID";
    static constexpr std::string_view type_name = "boost::uuids::uuid";

    static constexpr std::int32_t  byte_len  = 16;
    static constexpr bool supports_binary = true;

    static boost::system::error_code parse_binary(boost::span<const std::byte> bytes, value_type& val)
    {
        using boost::system::errc::invalid_argument;
        using boost::system::errc::make_error_code;

        // UUID must be exactly 16 bytes
        if (bytes.size() != byte_len)
            return make_error_code(invalid_argument);

        // PostgreSQL UUID binary format is just 16 raw bytes in network order.
        // No endianness conversion of the fields is required here; we just copy the bytes.
        for (std::size_t i = 0; i < static_cast<std::size_t>(byte_len); ++i)
        {
            val.data[i] = static_cast<boost::uuids::uuid::value_type>(
                std::to_integer<std::uint8_t>(bytes[i])
            );
        }

        return {}; // success
    }

    static boost::system::error_code parse_text(std::string_view str, value_type& val)
    {
        using boost::system::errc::invalid_argument;
        using boost::system::errc::make_error_code;

        // Expect canonical UUID text like "550e8400-e29b-41d4-a716-446655440000"
        try
        {
            val = boost::lexical_cast<boost::uuids::uuid>(std::string(str));
            return {}; // success
        }
        catch (const std::exception&)
        {
            return make_error_code(invalid_argument);
        }
    }

    static boost::system::error_code serialize_binary(value_type const& val, boost::span<std::byte>& bytes)
    {
        using boost::system::errc::invalid_argument;
        using boost::system::errc::make_error_code;

        // UUID must be exactly 16 bytes for serialization
        if (bytes.size() != byte_len)
            return make_error_code(invalid_argument);

        // PostgreSQL UUID binary format is 16 raw bytes; copy them out.
        for (std::size_t i = 0; i < static_cast<std::size_t>(byte_len); ++i)
        {
            bytes[i] = static_cast<std::byte>(val.data[i]);
        }

        return {}; // success
    }

    static boost::system::error_code serialize_text(value_type const& val, std::string& text)
    {
        // Use Boost.UUID's string conversion for canonical representation
        text = boost::uuids::to_string(val);
        return {}; // success
    }

};


inline boost::uuids::uuid to_uuid(std::string_view text)
{
    return boost::lexical_cast<boost::uuids::uuid>(text);
}


} // namespace types
} // namespace nativepg

#endif // NATIVEPG_TYPES_PG_UUID_HPP
