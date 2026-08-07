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

#include <cstdint>
#include <system_error>

#include "nativepg/extended_error.hpp"
#include "nativepg/field_traits.hpp"
#include "nativepg/field_view.hpp"
#include "nativepg/types/numeric.hpp"

namespace nativepg::detail {

inline constexpr std::int32_t numeric_oid = 1700;

}  // namespace nativepg::detail

namespace nativepg {

// --- Parse
// There is no serialization counterpart yet: nativepg/types/numeric.hpp implements
// parsing only.

// NUMERIC
template <unsigned Digits, class Exp, class Alloc, boost::multiprecision::expression_template_option ET>
struct parse_field_traits<
    boost::multiprecision::number<boost::multiprecision::cpp_dec_float<Digits, Exp, Alloc>, ET>>
{
    using value_type = boost::multiprecision::
        number<boost::multiprecision::cpp_dec_float<Digits, Exp, Alloc>, ET>;

    static std::error_code is_compatible(std::int32_t type_oid)
    {
        return type_oid == detail::numeric_oid ? std::error_code{} : client_errc::incompatible_field_type;
    }

    static std::error_code parse_text(field_view from, std::int32_t type_oid, value_type& to)
    {
        if (from.is_null())
            return client_errc::unexpected_null;
        BOOST_ASSERT(type_oid == detail::numeric_oid);
        return types::parse_text_numeric(from, to);
    }

    static std::error_code parse_binary(field_view from, std::int32_t type_oid, value_type& to)
    {
        if (from.is_null())
            return client_errc::unexpected_null;
        BOOST_ASSERT(type_oid == detail::numeric_oid);
        return types::parse_binary_numeric(from, to);
    }
};

}  // namespace nativepg

#endif
