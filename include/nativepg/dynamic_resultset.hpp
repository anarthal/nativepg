//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_DYNAMIC_RESULTSET_HPP
#define NATIVEPG_DYNAMIC_RESULTSET_HPP

// A structure similar to PGResult

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "nativepg/protocol/common.hpp"
#include "nativepg/protocol/describe.hpp"

namespace nativepg {

namespace detail {

struct offset_and_length
{
    std::size_t offset, length;
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
            .name = {reinterpret_cast<const char*>(data + name.offset), name.length},
            .table_oid = table_oid,
            .column_attribute = column_attribute,
            .type_oid = type_oid,
            .type_length = type_length,
            .type_modifier = type_modifier,
            .fmt_code = fmt_code,
        };
    }
};

}  // namespace detail

class field_descriptions_view
{
    std::span<const detail::offsetted_field_description> descrs_;
    const unsigned char* data_;

public:
    field_descriptions_view(
        std::span<const detail::offsetted_field_description> descr,
        const unsigned char* data
    ) noexcept
        : descrs_(descr), data_(data)
    {
    }
};

class dynamic_resultset
{
    std::vector<detail::offsetted_field_description> field_descr_;
    std::vector<detail::offset_and_length> values_;
    std::vector<unsigned char> data_;

public:
    dynamic_resultset() = default;
};

}  // namespace nativepg

#endif
