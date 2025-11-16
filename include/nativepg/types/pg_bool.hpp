//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_TYPES_PG_BOOL_HPP
#define NATIVEPG_TYPES_PG_BOOL_HPP

#include "nativepg/types/pg_type_traits.hpp"
#include "nativepg/types/basic_pg_value.hpp"

namespace nativepg {
namespace types {

template <>
struct pg_type_traits<bool, pg_oid_type::bool_oid>
{
    using value_type = bool;

    // Meta information
    static constexpr pg_oid_type oid_type = pg_oid_type::bool_oid;
    static constexpr std::int32_t byte_len = 1;

    static constexpr std::string_view oid_name  = "bool";
    static constexpr std::string_view type_name = "bool";

    static constexpr bool supports_binary = true;

    // ----- TEXT ENCODE / DECODE -----
    //
    // PostgreSQL text BOOL is "t" / "f"
    //
    static std::string encode_text(const value_type& v)
    {
        return v ? "t" : "f";
    }

    static value_type decode_text(std::string_view sv)
    {
        if (sv == "t" || sv == "true"  || sv == "1") return true;
        if (sv == "f" || sv == "false" || sv == "0") return false;

        throw std::runtime_error{"Invalid BOOL text: " + std::string{sv}};
    }

    // ----- BINARY ENCODE / DECODE -----
    //
    // PostgreSQL binary BOOL is:
    //     1 byte: 1 = true, 0 = false
    //
    static std::string encode_binary(const value_type& v)
    {
        std::string buf(1, '\0');
        buf[0] = static_cast<char>(v ? 1 : 0);
        return buf;
    }

    static value_type decode_binary(std::string_view sv)
    {
        if (sv.size() != 1)
            throw std::runtime_error{"bool binary length != 1"};

        unsigned char b = static_cast<unsigned char>(sv[0]);
        if (b == 1) return true;
        if (b == 0) return false;

        throw std::runtime_error{"Invalid BOOL binary value"};
    }
};


// Value Type declaration
using pg_bool = basic_pg_value<bool, pg_oid_type::bool_oid>;

}
}

#endif
