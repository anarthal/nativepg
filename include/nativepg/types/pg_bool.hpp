//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_TYPES_PG_BOOL_HPP
#define NATIVEPG_TYPES_PG_BOOL_HPP

#include "nativepg/types/pg_type_traits.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <boost/core/span.hpp>
#include <boost/system/error_code.hpp>
#include <boost/endian/conversion.hpp>

namespace nativepg {
namespace types {

template <>
struct pg_type_traits<bool, pg_oid_type::bool_>
{
    using value_type = bool;

    // Meta information
    static constexpr pg_oid_type oid_type = pg_oid_type::bool_;

    static constexpr std::string_view oid_name  = "BOOL";
    static constexpr std::string_view type_name = "bool";

    static constexpr std::int32_t byte_len = 1;
    static constexpr bool supports_binary = true;

    static boost::system::error_code parse_binary(boost::span<const std::byte> bytes, value_type& val)
    {
        using boost::system::errc::invalid_argument;
        using boost::system::errc::make_error_code;

        // BOOL must be exactly 1 byte
        if (bytes.size() != byte_len)
            return make_error_code(invalid_argument);

        const std::uint8_t b = static_cast<std::uint8_t>(bytes[0]);

        if (b == 1)
        {
            val = true;
            return {};
        }
        if (b == 0)
        {
            val = false;
            return {};
        }

        // Any other value is invalid for PostgreSQL BOOL
        return make_error_code(invalid_argument);
    }
    static boost::system::error_code parse_text(std::string const& str, value_type& val)
    {
        using boost::system::errc::invalid_argument;
        using boost::system::errc::make_error_code;

        std::string_view text(str);

        // Normalize to lowercase for robust comparison
        auto to_lower = [](char c) -> char {
            if (c >= 'A' && c <= 'Z')
                return static_cast<char>(c - 'A' + 'a');
            return c;
        };

        std::string tmp;
        tmp.reserve(text.size());
        for (char c : text)
            tmp.push_back(to_lower(c));

        if (tmp == "t" || tmp == "true" || tmp == "1")
        {
            val = true;
            return {};
        }
        if (tmp == "f" || tmp == "false" || tmp == "0")
        {
            val = false;
            return {};
        }

        return make_error_code(invalid_argument);
    }


    static boost::system::error_code serialize_binary(value_type const& val, boost::span<std::byte>& bytes)
    {
        using boost::system::errc::invalid_argument;
        using boost::system::errc::make_error_code;

        // BOOL must be exactly 1 byte for serialization
        if (bytes.size() != byte_len)
            return make_error_code(invalid_argument);

        // PostgreSQL BOOL binary format: 1 = true, 0 = false
        std::uint8_t v = val ? 1u : 0u;

        // Endianness is irrelevant for a single byte, but we keep the style consistent.
        std::uint8_t network_value = boost::endian::native_to_big(v);

        bytes[0] = static_cast<std::byte>(network_value);
        return {};
    }

    static boost::system::error_code serialize_text(value_type const& val, std::string& text)
    {
        // PostgreSQL canonical text for BOOL is "t" / "f"
        text = val ? "t" : "f";
        return {};
    }
};


template <>
struct pg_type_from_oid<pg_oid_type::bool_>
{
    using type = bool;
};


// Trait type
using pg_bool_traits = pg_type_traits<pg_type_from_oid<pg_oid_type::bool_>::type, pg_oid_type::bool_>;

}
}

#endif
