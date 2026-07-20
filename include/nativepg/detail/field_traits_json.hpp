//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_DETAIL_FIELD_TRAITS_JSON_HPP
#define NATIVEPG_DETAIL_FIELD_TRAITS_JSON_HPP

// This header is opt-in: it's included by nativepg/types/json.hpp, which is itself opt-in. Don't
// include it directly unless you also need nativepg/types/json.hpp's parsing functions.

#include <boost/json/value.hpp>
#include <boost/system/error_code.hpp>

#include <cstdint>

#include "nativepg/client_errc.hpp"
#include "nativepg/field_view.hpp"
#include "nativepg/protocol/describe.hpp"

namespace nativepg::detail {

inline constexpr std::int32_t json_oid = 114;
inline constexpr std::int32_t jsonb_oid = 3802;

template <class T>
struct field_is_compatible;

// JSON & JSONB
template <>
struct field_is_compatible<boost::json::value>
{
    static inline boost::system::error_code call(const protocol::field_description& desc)
    {
        return (desc.type_oid == json_oid || desc.type_oid == jsonb_oid)
                   ? boost::system::error_code()
                   : client_errc::incompatible_field_type;
    }
};

template <class T>
struct field_parse;

// JSON(B) => boost::json::value
template <>
struct field_parse<boost::json::value>
{
    static inline boost::system::error_code call(
        const field_view& from,
        const protocol::field_description& desc,
        boost::json::value& to
    )
    {
        if (from.is_null())
            return client_errc::unexpected_null;

        if (desc.type_oid == jsonb_oid)
        {
            return desc.fmt_code == protocol::format_code::text ? types::parse_json(from.data_str(), to)
                                                                : types::parse_binary_jsonb(from, to);
        }
        else
        {
            return desc.fmt_code == protocol::format_code::text ? types::parse_json(from.data_str(), to)
                                                                : types::parse_json(from.data_str(), to);
        }
    }
};

}  // namespace nativepg::detail

#endif  // NATIVEPG_DETAIL_FIELD_TRAITS_JSON_HPP
