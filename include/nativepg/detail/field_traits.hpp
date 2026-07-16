//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_DETAIL_FIELD_TRAITS_HPP
#define NATIVEPG_DETAIL_FIELD_TRAITS_HPP

#include <boost/system/error_code.hpp>

#include <concepts>
#include <span>
#include <string_view>

#include "nativepg/client_errc.hpp"
#include "nativepg/field_view.hpp"
#include "nativepg/protocol/describe.hpp"

// TODO: at some point we will need to expose some customization
namespace nativepg::detail {

// --- Is a type compatible with what we get from DB?
// TODO: string diagnostics
template <class T>
struct field_is_compatible;

// Strings can be used to parse any type
template <class T>
    requires std::assignable_from<T&, std::string_view>
struct field_is_compatible<T>
{
    static boost::system::error_code call(const protocol::field_description&)
    {
        return boost::system::error_code{};
    }
};

// --- Parse
template <class T>
struct field_parse;

template <class T>
    requires std::assignable_from<T&, std::string_view>
struct field_parse<T>
{
    static boost::system::error_code call(const field_view from, const protocol::field_description&, T& to)
    {
        if (from.is_null())
            return client_errc::unexpected_null;
        const auto data = from.data();
        to = std::string_view(reinterpret_cast<const char*>(data.data()), data.size());
        return {};
    }
};

}  // namespace nativepg::detail

#include "field_traits_base.hpp"
#include "field_traits_datetime.hpp"

// field_traits_numeric.hpp and field_traits_decimal.hpp are intentionally NOT included here: they're
// opt-in features. Include nativepg/types/numeric.hpp or nativepg/types/decimal.hpp directly (in the TU
// that needs them) to pull in Boost.Multiprecision/Boost.Decimal support.

#endif
