//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_TYPES_PG_INT4_HPP
#define NATIVEPG_TYPES_PG_INT4_HPP

#include <cstddef>

#include "nativepg/types/pg_type_traits.hpp"
#include "nativepg/types/basic_pg_value.hpp"


namespace nativepg {
namespace types {


template <>
struct pg_type_traits<std::int32_t, pg_oid_type::int4>
{
    using value_type = std::int32_t;

    static constexpr pg_oid_type oid_type = pg_oid_type::int4;
    static constexpr std::int32_t byte_len = 4;

    static constexpr std::string_view oid_name  = "INT4";
    static constexpr std::string_view type_name = "std::int32_t";

    static constexpr bool supports_binary = true;

    static std::string encode_text(const value_type& v)
    {
        return std::to_string(v);
    }

    static value_type decode_text(std::string_view sv)
    {
        return static_cast<std::int32_t>(std::stoi(std::string{sv}));
    }

    static std::string encode_binary(const value_type& v)
    {
        // Big-endian 4-byte int
        std::string buf(4, '\0');
        buf[0] = static_cast<char>((v >> 24) & 0xFF);
        buf[1] = static_cast<char>((v >> 16) & 0xFF);
        buf[2] = static_cast<char>((v >> 8)  & 0xFF);
        buf[3] = static_cast<char>((v)       & 0xFF);
        return buf;
    }

    static value_type decode_binary(std::string_view sv)
    {
        if (sv.size() != 4)
            throw std::runtime_error{"int4 binary length != 4"};

        const unsigned char b0 = static_cast<unsigned char>(sv[0]);
        const unsigned char b1 = static_cast<unsigned char>(sv[1]);
        const unsigned char b2 = static_cast<unsigned char>(sv[2]);
        const unsigned char b3 = static_cast<unsigned char>(sv[3]);

        std::int32_t v = (static_cast<std::int32_t>(b0) << 24) |
                         (static_cast<std::int32_t>(b1) << 16) |
                         (static_cast<std::int32_t>(b2) << 8)  |
                         (static_cast<std::int32_t>(b3));
        return v;
    }
};


template <>
struct pg_type_from_oid<pg_oid_type::int4>
{
    using type = std::int32_t;
};


// Trait type
using pg_int4_traits = pg_type_traits<pg_type_from_oid<pg_oid_type::int4>::type, pg_oid_type::int4>;

// Value Type declaration
using pg_int4 = basic_pg_value<pg_int4_traits>;


}
}

#endif
