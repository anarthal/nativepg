//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_TYPES_PG_FLOAT4_HPP
#define NATIVEPG_TYPES_PG_FLOAT4_HPP

#include <bit>
#include <string>
#include <string_view>
#include <stdexcept>

#include "nativepg/types/pg_type_traits.hpp"
#include "nativepg/types/basic_pg_value.hpp"

namespace nativepg {
namespace types {

template <>
struct pg_type_traits<float, pg_oid_type::float4>
{
    using value_type = float;

    static constexpr pg_oid_type   oid_type  = pg_oid_type::float4;
    static constexpr std::int32_t  byte_len  = 4;

    static constexpr std::string_view oid_name  = "FLOAT4";
    static constexpr std::string_view type_name = "float";

    static constexpr bool supports_binary = true;

    // ----- TEXT ENCODE / DECODE -----
    static std::string encode_text(const value_type& v)
    {
        return std::to_string(v);
    }

    static value_type decode_text(std::string_view sv)
    {
        return std::stof(std::string{sv});
    }

    // ----- BINARY ENCODE / DECODE -----
    static std::string encode_binary(const value_type& v)
    {
        // PostgreSQL uses IEEE-754, network (big-endian) byte order
        std::uint32_t bits = std::bit_cast<std::uint32_t>(v);

        std::string buf(4, '\0');
        buf[0] = static_cast<char>((bits >> 24) & 0xFF);
        buf[1] = static_cast<char>((bits >> 16) & 0xFF);
        buf[2] = static_cast<char>((bits >> 8)  & 0xFF);
        buf[3] = static_cast<char>((bits)       & 0xFF);
        return buf;
    }

    static value_type decode_binary(std::string_view sv)
    {
        if (sv.size() != 4)
            throw std::runtime_error{"float4 binary length != 4"};

        const unsigned char b0 = static_cast<unsigned char>(sv[0]);
        const unsigned char b1 = static_cast<unsigned char>(sv[1]);
        const unsigned char b2 = static_cast<unsigned char>(sv[2]);
        const unsigned char b3 = static_cast<unsigned char>(sv[3]);

        std::uint32_t bits =
            (static_cast<std::uint32_t>(b0) << 24) |
            (static_cast<std::uint32_t>(b1) << 16) |
            (static_cast<std::uint32_t>(b2) << 8)  |
            (static_cast<std::uint32_t>(b3));

        return std::bit_cast<value_type>(bits);
    }
};


// OID -> C++ type mapping
template <>
struct pg_type_from_oid<pg_oid_type::float4>
{
    using type = float;
};


// Trait and value aliases
using pg_float4_traits = pg_type_traits<
    pg_type_from_oid<pg_oid_type::float4>::type,
    pg_oid_type::float4
>;

using pg_float4 = basic_pg_value<pg_float4_traits>;

} // namespace types
} // namespace nativepg

#endif // NATIVEPG_TYPES_PG_FLOAT4_HPP
