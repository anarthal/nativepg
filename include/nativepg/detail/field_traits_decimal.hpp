//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_DETAIL_FIELD_TRAITS_DECIMAL_HPP
#define NATIVEPG_DETAIL_FIELD_TRAITS_DECIMAL_HPP

// This header is opt-in: it's included by nativepg/types/decimal.hpp, which is itself opt-in. Don't
// include it directly unless you also need nativepg/types/decimal.hpp's parsing functions.

#include <boost/assert.hpp>
#include <boost/decimal/decimal128_t.hpp>
#include <boost/decimal/decimal32_t.hpp>
#include <boost/decimal/decimal64_t.hpp>

#include <concepts>
#include <cstdint>
#include <system_error>

#include "nativepg/extended_error.hpp"
#include "nativepg/field_traits.hpp"
#include "nativepg/field_view.hpp"
#include "nativepg/types/decimal.hpp"

namespace nativepg::detail {

inline constexpr std::int32_t decimal_oid = 1700; /* same as numeric_oid */

// The boost::decimal types we support
template <class T>
concept is_decimal = std::same_as<T, boost::decimal::decimal32_t> ||
                     std::same_as<T, boost::decimal::decimal64_t> ||
                     std::same_as<T, boost::decimal::decimal128_t>;

}  // namespace nativepg::detail

namespace nativepg {

// --- Parse
// There is no serialization counterpart yet: nativepg/types/decimal.hpp implements
// parsing only.

// NUMERIC
template <detail::is_decimal T>
struct parse_field_traits<T>
{
    static std::error_code is_compatible(std::int32_t type_oid)
    {
        return type_oid == detail::decimal_oid ? std::error_code{} : client_errc::incompatible_field_type;
    }

    static std::error_code parse_text(field_view from, std::int32_t type_oid, T& to)
    {
        if (from.is_null())
            return client_errc::unexpected_null;
        BOOST_ASSERT(type_oid == detail::decimal_oid);
        return types::parse_text_decimal(from, to);
    }

    static std::error_code parse_binary(field_view from, std::int32_t type_oid, T& to)
    {
        if (from.is_null())
            return client_errc::unexpected_null;
        BOOST_ASSERT(type_oid == detail::decimal_oid);
        return types::parse_binary_decimal(from, to);
    }
};

}  // namespace nativepg

#endif
