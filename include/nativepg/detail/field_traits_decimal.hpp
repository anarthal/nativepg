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
#include <boost/system/error_code.hpp>

#include <concepts>
#include <cstdint>

#include "nativepg/extended_error.hpp"
#include "nativepg/protocol/describe.hpp"
#include "nativepg/types/decimal.hpp"

namespace nativepg::detail {

inline constexpr std::int32_t decimal_oid = 1700; /* same as numeric_oid */

template <class T>
struct field_is_compatible;

template <class T>
    requires std::same_as<T, boost::decimal::decimal32_t> || std::same_as<T, boost::decimal::decimal64_t> ||
             std::same_as<T, boost::decimal::decimal128_t>
struct field_is_compatible<T>
{
    static boost::system::error_code call(const protocol::field_description& desc)
    {
        return desc.type_oid == decimal_oid ? boost::system::error_code{}
                                            : client_errc::incompatible_field_type;
    }
};

// --- Parse
template <class T>
struct field_parse;

template <class T>
    requires std::same_as<T, boost::decimal::decimal32_t> || std::same_as<T, boost::decimal::decimal64_t> ||
             std::same_as<T, boost::decimal::decimal128_t>
struct field_parse<T>
{
    static boost::system::error_code call(
        const field_view& from,
        const protocol::field_description& desc,
        T& to
    )
    {
        BOOST_ASSERT(desc.type_oid == decimal_oid);
        return desc.fmt_code == protocol::format_code::text ? types::parse_text_decimal(from, to)
                                                            : types::parse_binary_decimal(from, to);
    }
};

}  // namespace nativepg::detail

#endif
