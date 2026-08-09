//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_DETAIL_FIELD_TRAITS_NULLABLE_HPP
#define NATIVEPG_DETAIL_FIELD_TRAITS_NULLABLE_HPP

#pragma once

#include <cstdint>
#include <optional>
#include <system_error>
#include <type_traits>
#include <vector>

#include "nativepg/client_errc.hpp"
#include "nativepg/field_traits.hpp"
#include "nativepg/field_view.hpp"
#include "nativepg/protocol/common.hpp"

namespace nativepg::detail {

// Is a type an optional?
template <class T>
struct is_optional : std::false_type
{
};

template <class T>
struct is_optional<std::optional<T>> : std::true_type
{
};

template <class T>
inline constexpr auto is_optional_v = is_optional<T>::value;

}  // namespace nativepg::detail

namespace nativepg {

// --- Parse
template <parsable_field T>
struct parse_field_traits<std::optional<T>>
{
    static_assert(
        !detail::is_optional_v<T>,
        "Nested std::optional (e.g. std::optional<std::optional<T>>) is not supported"
    );

    static std::error_code is_compatible(std::int32_t type_oid) { return field_is_compatible<T>(type_oid); }

    static std::error_code parse(
        field_view from,
        std::int32_t type_oid,
        protocol::format_code code,
        std::optional<T>& to
    )
    {
        if (from.is_null())
        {
            to.reset();
            return std::error_code{};
        }
        return field_parse(from, type_oid, code, to.emplace());
    }
};

// --- Serialize
template <serializable_field T>
struct serialize_field_traits<std::optional<T>>
{
    static_assert(
        !detail::is_optional_v<T>,
        "Nested std::optional (e.g. std::optional<std::optional<T>>) is not supported"
    );

    // A NULL has no type of its own, so we advertise the value type's OID
    static constexpr std::int32_t oid = field_serialize_oid<T>;

    static std::error_code serialize(
        const std::optional<T>& value,
        protocol::format_code code,
        std::vector<unsigned char>& to
    )
    {
        // serialize_null indicates that a NULL value should be serialized (not an error)
        return value.has_value() ? field_serialize(*value, code, to) : client_errc::serialize_null;
    }
};

}  // namespace nativepg

#endif
