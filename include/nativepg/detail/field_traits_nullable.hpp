//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_DETAIL_FIELD_TRAITS_NULLABLE_HPP
#define NATIVEPG_DETAIL_FIELD_TRAITS_NULLABLE_HPP

#include <boost/system/error_code.hpp>

#include <span>
#include <string>
#include <optional>
#include <utility>

#include "nativepg/protocol/describe.hpp"

namespace nativepg::detail {


// --- Is a type compatible with what we get from DB?
template <class T>
struct field_is_compatible;

template <typename T>
struct field_is_compatible<std::optional<T>>
{
    static boost::system::error_code call(const protocol::field_description& desc)
    {
        return field_is_compatible<T>::call(desc);
    }
};

// --- Parse
template <class T>
struct field_parse;

template <typename T>
struct field_parse<std::optional<T>>
{
    static boost::system::error_code call(
        const field_view& from,
        const protocol::field_description& desc,
        std::optional<T>& to
    )
    {
        if (from.is_null())
        {
            to.reset();
            return boost::system::error_code{};
        }
        T value{};
        const auto ec = field_parse<T>::call(from, desc, value);
        if (!ec)
            // TODO: Check if std::make_optional(value) should be used heir?
            to = std::move(value);
        return ec;
    }
};

}  // namespace nativepg::detail

#endif
