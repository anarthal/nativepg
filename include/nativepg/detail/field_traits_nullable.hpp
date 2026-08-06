//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_DETAIL_FIELD_TRAITS_NULLABLE_HPP
#define NATIVEPG_DETAIL_FIELD_TRAITS_NULLABLE_HPP

#pragma once

#include <boost/system/error_code.hpp>

#include <optional>
#include <type_traits>

#include "nativepg/field_traits.hpp"
#include "nativepg/field_view.hpp"
#include "nativepg/protocol/describe.hpp"

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
// A NULL field yields an empty optional, rather than an error.
// There is no serialization counterpart yet: serialize_field_traits has no way
// to express a NULL parameter.
template <parsable_field T>
struct parse_field_traits<std::optional<T>>
{
    static_assert(
        !detail::is_optional_v<T>,
        "Nested std::optional (e.g. std::optional<std::optional<T>>) is not supported"
    );

    static boost::system::error_code is_compatible(const protocol::field_description& desc)
    {
        return field_is_compatible<T>(desc);
    }

    static boost::system::error_code parse(
        field_view from,
        const protocol::field_description& desc,
        std::optional<T>& to
    )
    {
        if (from.is_null())
        {
            to.reset();
            return std::error_code{};
        }
        return field_parse(from, desc, to.emplace());
    }
};

}  // namespace nativepg

#endif
