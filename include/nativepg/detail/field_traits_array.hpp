//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_DETAIL_FIELD_TRAITS_ARRAY_HPP
#define NATIVEPG_DETAIL_FIELD_TRAITS_ARRAY_HPP

// This header is opt-in: it's included by nativepg/types/array.hpp, which is itself opt-in.

#include <cstdint>
#include <span>
#include <system_error>
#include <vector>

#include "nativepg/client_errc.hpp"
#include "nativepg/field_traits.hpp"
#include "nativepg/field_view.hpp"
#include "nativepg/protocol/common.hpp"
#include "nativepg/types/array.hpp"

namespace nativepg {

// --- Parse
// PostgreSQL array => std::vector<T>
template <parsable_field T>
struct parse_field_traits<std::vector<T>>
{
    static std::error_code is_compatible(std::int32_t type_oid)
    {
        const auto element_oid = types::element_oid_for_array(type_oid);
        if (element_oid == 0)
            return client_errc::incompatible_field_type;
        return field_is_compatible<T>(element_oid);
    }

    static std::error_code parse(
        field_view from,
        std::int32_t type_oid,
        protocol::format_code code,
        std::vector<T>& to
    )
    {
        if (from.is_null())
            return client_errc::unexpected_null;
        return code == protocol::format_code::binary ? types::parse_binary_array(from, type_oid, to)
                                                     : types::parse_text_array(from, type_oid, to);
    }
};

// --- Serialize
// std::vector<T> => PostgreSQL array
template <serializable_field T>
struct serialize_field_traits<std::vector<T>>
{
    static_assert(
        types::array_oid_for_element(field_serialize_oid<T>) != 0,
        "The element type has no known PostgreSQL array type"
    );

    static constexpr std::int32_t oid = types::array_oid_for_element(field_serialize_oid<T>);

    static std::error_code serialize(
        const std::vector<T>& value,
        protocol::format_code code,
        std::vector<unsigned char>& to
    )
    {
        return types::serialize_array(value, field_serialize_oid<T>, code, to);
    }
};

// std::span<const T> => PostgreSQL array
template <serializable_field T>
struct serialize_field_traits<std::span<const T>>
{
    static_assert(
        types::array_oid_for_element(field_serialize_oid<T>) != 0,
        "The element type has no known PostgreSQL array type"
    );

    static constexpr std::int32_t oid = types::array_oid_for_element(field_serialize_oid<T>);

    static std::error_code serialize(
        const std::span<const T>& value,
        protocol::format_code code,
        std::vector<unsigned char>& to
    )
    {
        return types::serialize_array(value, field_serialize_oid<T>, code, to);
    }
};

}  // namespace nativepg

#endif  // NATIVEPG_DETAIL_FIELD_TRAITS_ARRAY_HPP
