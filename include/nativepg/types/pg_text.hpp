//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_TYPES_PG_TEXT_HPP
#define NATIVEPG_TYPES_PG_TEXT_HPP

#include <cstddef>
#include <string>
#include <string_view>

#include "nativepg/types/pg_type_traits.hpp"
#include "nativepg/types/basic_pg_value.hpp"

namespace nativepg {
namespace types {

template <>
struct pg_type_traits<std::string, pg_oid_type::text>
{
    using value_type = std::string;

    static constexpr pg_oid_type   oid_type  = pg_oid_type::text;
    static constexpr std::int32_t  byte_len  = -1; // variable length

    static constexpr std::string_view oid_name  = "TEXT";
    static constexpr std::string_view type_name = "std::string";

    static constexpr bool supports_binary = true;

    // ----- TEXT ENCODE / DECODE -----
    static std::string encode_text(const value_type& v)
    {
        // PostgreSQL text representation is just the string bytes
        return v;
    }

    static value_type decode_text(std::string_view sv)
    {
        return std::string{sv};
    }

    // ----- BINARY ENCODE / DECODE -----
    static std::string encode_binary(const value_type& v)
    {
        // Binary format is also just the raw bytes
        return v;
    }

    static value_type decode_binary(std::string_view sv)
    {
        return std::string{sv};
    }
};


// OID -> C++ type mapping
template <>
struct pg_type_from_oid<pg_oid_type::text>
{
    using type = std::string;
};


// Trait type
using pg_text_traits = pg_type_traits<
    pg_type_from_oid<pg_oid_type::text>::type,
    pg_oid_type::text
>;

// Value Type alias
using pg_text = basic_pg_value<pg_text_traits>;

} // namespace types
} // namespace nativepg

#endif // NATIVEPG_TYPES_PG_TEXT_HPP
