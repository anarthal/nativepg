//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_DETAIL_FIELD_TRAITS_NUMERIC_HPP
#define NATIVEPG_DETAIL_FIELD_TRAITS_NUMERIC_HPP

// This header is opt-in: it's included by nativepg/types/numeric.hpp, which is itself opt-in. Don't
// include it directly unless you also need nativepg/types/numeric.hpp's parsing functions.

#include <boost/assert.hpp>
#include <boost/multiprecision/cpp_dec_float.hpp>
#include <boost/multiprecision/number.hpp>
#include <boost/system/error_code.hpp>

#include <cstdint>

#include "nativepg/extended_error.hpp"
#include "nativepg/protocol/describe.hpp"
#include "nativepg/types/numeric.hpp"

namespace nativepg::detail {

inline constexpr std::int32_t numeric_oid = 1700;

template <class T>
struct field_is_compatible;

template <unsigned Digits, class Exp, class Alloc, boost::multiprecision::expression_template_option ET>
struct field_is_compatible<
    boost::multiprecision::number<boost::multiprecision::cpp_dec_float<Digits, Exp, Alloc>, ET>>
{
    static boost::system::error_code call(const protocol::field_description& desc)
    {
        return desc.type_oid == numeric_oid ? boost::system::error_code{}
                                            : client_errc::incompatible_field_type;
    }
};

// --- Parse
template <class T>
struct field_parse;

template <unsigned Digits, class Exp, class Alloc, boost::multiprecision::expression_template_option ET>
struct field_parse<
    boost::multiprecision::number<boost::multiprecision::cpp_dec_float<Digits, Exp, Alloc>, ET>>
{
    static boost::system::error_code call(
        const field_view& from,
        const protocol::field_description& desc,
        boost::multiprecision::number<boost::multiprecision::cpp_dec_float<Digits, Exp, Alloc>, ET>& to
    )
    {
        if (from.is_null())
            return client_errc::unexpected_null;
        BOOST_ASSERT(desc.type_oid == numeric_oid);
        return desc.fmt_code == protocol::format_code::text ? types::parse_text_numeric(from, to)
                                                            : types::parse_binary_numeric(from, to);
    }
};

}  // namespace nativepg::detail

#endif
