//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_DETAIL_FIELD_TRAITS_DECIMAL_HPP
#define NATIVEPG_DETAIL_FIELD_TRAITS_DECIMAL_HPP

#ifdef NATIVEPG_USE_DECIMAL_TYPES

#include <boost/decimal.hpp>
#include <boost/system/error_code.hpp>

#include <span>

#include "nativepg/extended_error.hpp"
#include "nativepg/protocol/describe.hpp"
#include "nativepg/types/decimal.hpp"


namespace nativepg::detail {

namespace bd = boost::decimal;

inline constexpr std::int32_t decimal_oid = 1700; /* same as numeric_oid */

template <class T>
struct field_is_compatible;

template <class T>
    requires std::same_as<T, bd::decimal32_t> ||
             std::same_as<T, bd::decimal64_t> ||
             std::same_as<T, bd::decimal128_t>
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
    requires std::same_as<T, bd::decimal32_t> ||
             std::same_as<T, bd::decimal64_t> ||
             std::same_as<T, bd::decimal128_t>
struct field_parse<T>
{
    static boost::system::error_code call(
        const field_view& from,
        const protocol::field_description& desc,
        T& to)
    {
        if (from.is_null())
            return client_errc::unexpected_null;
        BOOST_ASSERT(desc.type_oid == decimal_oid);
        return desc.fmt_code == protocol::format_code::text
                 ? types::parse_text_decimal(from.data(), to)
                 : types::parse_binary_decimal(from.data(), to);
    }
};

}// namespace nativepg::detail
#endif

#endif
