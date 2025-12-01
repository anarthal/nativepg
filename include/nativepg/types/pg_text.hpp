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
#include <boost/core/span.hpp>
#include <boost/system/error_code.hpp>
#include <boost/endian/conversion.hpp>

#include "nativepg/types/pg_type_traits.hpp"

namespace nativepg {
namespace types {

template <>
struct pg_type_traits<std::string, pg_oid_type::text>
{
    using value_type = std::string;

    static constexpr pg_oid_type   oid_type  = pg_oid_type::text;

    static constexpr std::string_view oid_name  = "TEXT";
    static constexpr std::string_view type_name = "std::string";

    static constexpr std::int32_t  byte_len  = -1; // variable length
    static constexpr bool supports_binary = true;

    static boost::system::error_code parse_binary(boost::span<const std::byte> bytes, value_type& val)
    {
        // For TEXT, PostgreSQL binary representation is just the raw bytes of the string.
        // No endianness conversion is required.
        val.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        return {}; // success
    }

    static boost::system::error_code parse_text(std::string const& str, value_type& val)
    {
        // Text representation is already a string; just copy it.
        val = str;
        return {}; // success
    }


    static boost::system::error_code serialize_binary(value_type const& val, boost::span<std::byte>& bytes)
    {
        using boost::system::errc::no_buffer_space;
        using boost::system::errc::make_error_code;

        // For TEXT, binary format is the raw bytes of the string.
        // Ensure the output buffer is large enough.
        if (bytes.size() < val.size())
            return make_error_code(no_buffer_space);

        const auto* data = reinterpret_cast<const std::byte*>(val.data());
        std::copy(data, data + val.size(), bytes.begin());

        return {}; // success
    }

    static boost::system::error_code serialize_text(value_type const& val, std::string& text)
    {
        // Text representation is just the value itself.
        text = val;
        return {}; // success
    }

};


} // namespace types
} // namespace nativepg

#endif // NATIVEPG_TYPES_PG_TEXT_HPP
