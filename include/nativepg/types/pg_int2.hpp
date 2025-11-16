//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_TYPES_PG_INT2_HPP
#define NATIVEPG_TYPES_PG_INT2_HPP

#include <cstddef>

#include "nativepg/types/pg_type_traits.hpp"
#include "nativepg/types/basic_pg_value.hpp"


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

    // ----- TEXT ENCODE / DECODE -----
    static std::string encode_text(const value_type& v)
    {
        return std::to_string(v);
    }

    static value_type decode_text(std::string_view sv)
    {
        return static_cast<std::int16_t>(std::stoi(std::string{sv}));
    }

    // ----- BINARY ENCODE / DECODE -----
    static std::string encode_binary(const value_type& v)
    {
        // int2 is big-endian 2-byte signed integer
        std::string buf(2, '\0');
        buf[0] = static_cast<char>((v >> 8) & 0xFF);
        buf[1] = static_cast<char>((v)      & 0xFF);
        return buf;
    }

    static value_type decode_binary(std::string_view sv)
    {
        if (sv.size() != 2)
            throw std::runtime_error{"int2 binary length != 2"};

        const unsigned char b0 = static_cast<unsigned char>(sv[0]);
        const unsigned char b1 = static_cast<unsigned char>(sv[1]);

        // big-endian decode
        std::int16_t v =
            (static_cast<std::int16_t>(b0) << 8) |
            (static_cast<std::int16_t>(b1));

        return v;
    }
};


// Value Type declaration
using pg_int2 = basic_pg_value<std::int16_t, pg_oid_type::int2>;


}
}

#endif
