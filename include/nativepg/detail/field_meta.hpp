//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_DETAIL_FIELD_META_HPP
#define NATIVEPG_DETAIL_FIELD_META_HPP

#include <cstdint>
#include <optional>
#include <string_view>

#include "nativepg/client_errc.hpp"
#include "nativepg/protocol/describe.hpp"


namespace nativepg::detail {

// Forward declaration — defined in field_traits.hpp
template <class T>
struct field_is_compatible;

// Default: column name = member name (from BOOST_DESCRIBE), no OID override, not nullable
template <auto MemberPtr>
struct field_meta
{
    // Column name override: nullopt means "use the member name"
    static constexpr std::optional<std::string_view> column_name = std::nullopt;

    // OID override: nullopt means "accept whatever field_is_compatible says"
    static constexpr std::optional<std::int32_t> oid = std::nullopt;

    // Whether NULL is allowed (maps to std::optional<T> on the C++ side)
    static constexpr bool nullable = false;
};

// Check OID compatibility honoring field_meta override.
// If field_meta<MemberPtr>::oid has a value, it must match exactly.
// Otherwise, fall through to the type-based field_is_compatible<FieldType> check.
template <auto MemberPtr, class FieldType>
inline boost::system::error_code check_field_compatible(
    const protocol::field_description& desc)
{
    constexpr auto oid_override = field_meta<MemberPtr>::oid;
    if constexpr (oid_override.has_value())
    {
        // User pinned a specific OID — enforce it strictly
        return desc.type_oid == *oid_override
            ? boost::system::error_code{}
        : client_errc::incompatible_field_type;
    }
    else
    {
        // No override — delegate to the type-based trait
        return field_is_compatible<FieldType>::call(desc);
    }
}

}  // namespace nativepg::detail

#endif
