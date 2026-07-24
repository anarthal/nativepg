//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_DYNAMIC_UTILS_HPP
#define NATIVEPG_DYNAMIC_UTILS_HPP

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "nativepg/field_view.hpp"
#include "nativepg/protocol/common.hpp"
#include "nativepg/protocol/describe.hpp"

namespace nativepg::detail {

struct offset_and_length
{
    std::size_t offset, length;

    field_view to_field_view(const unsigned char* data) const
    {
        // Lengths are limited to INT32_MAX, so we can use -1 to represent NULL
        if (length == static_cast<std::size_t>(-1))
            return field_view();
        return std::span<const unsigned char>{data + offset, length};
    }

    std::string_view to_string_view(const unsigned char* data) const
    {
        return {reinterpret_cast<const char*>(data) + offset, length};
    }
};

// Like protocol::field_description, but strings are offset based
struct offsetted_field_description
{
    offset_and_length name;
    std::int32_t table_oid;
    std::int16_t column_attribute;
    std::int32_t type_oid;
    std::int16_t type_length;
    std::int32_t type_modifier;
    protocol::format_code fmt_code;

    protocol::field_description to_field_description(const unsigned char* data) const
    {
        return {
            .name = name.to_string_view(data),
            .table_oid = table_oid,
            .column_attribute = column_attribute,
            .type_oid = type_oid,
            .type_length = type_length,
            .type_modifier = type_modifier,
            .fmt_code = fmt_code,
        };
    }
};

}  // namespace nativepg::detail

#endif
