//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_DETAIL_FIELD_TRAITS_NUMERIC_HPP
#define NATIVEPG_DETAIL_FIELD_TRAITS_NUMERIC_HPP

#ifdef NATIVEPG_USE_NUMERIC_TYPES

#include <boost/multiprecision/cpp_dec_float.hpp>
#include <boost/multiprecision/number.hpp>
#include <boost/system/error_code.hpp>

#include <span>

#include "nativepg/extended_error.hpp"
#include "nativepg/protocol/describe.hpp"
#include "nativepg/types/numeric.hpp"


namespace nativepg::detail {

namespace mp = boost::multiprecision;

inline constexpr std::int32_t numeric_oid = 1700;


template <class T>
struct field_is_compatible;

template <std::size_t TDigits>
struct field_is_compatible<mp::number<mp::cpp_dec_float<TDigits>>>
{
    static inline boost::system::error_code call(const protocol::field_description& desc)
    {
        if (desc.type_oid == numeric_oid)
            return boost::system::error_code{};

        return client_errc::incompatible_field_type;
    }
};


// --- Parse
template <class T>
struct field_parse;

template <std::size_t TDigits>
struct field_parse<mp::number<mp::cpp_dec_float<TDigits>>>
{
    static inline boost::system::error_code call(
        const field_view& from,
        const protocol::field_description& desc,
        mp::cpp_dec_float<TDigits>& to
    )
    {
        if (from.is_null())
            return client_errc::unexpected_null;
        BOOST_ASSERT(desc.type_oid == numeric_oid);
        return desc.fmt_code == protocol::format_code::text ? types::parse_text_numeric<TDigits>(from.data(), to)
                                                        : types::parse_binary_numeric<TDigits>(from.data(), to);
    }
};


}// namespace nativepg::detail
#endif

#endif
