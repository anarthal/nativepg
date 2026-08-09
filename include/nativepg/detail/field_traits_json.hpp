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

#include <boost/assert.hpp>
#include <boost/json/value.hpp>

#include <cstdint>
#include <system_error>

#include "nativepg/client_errc.hpp"
#include "nativepg/field_traits.hpp"
#include "nativepg/field_view.hpp"
#include "nativepg/protocol/common.hpp"
#include "nativepg/types/json.hpp"

namespace nativepg::detail {

inline constexpr std::int32_t json_oid = 114;
inline constexpr std::int32_t jsonb_oid = 3802;

}  // namespace nativepg::detail

namespace nativepg {

// --- Parse
// There is no serialization counterpart: nativepg/types/json.hpp implements parsing only,
// so these types can't be used as query parameters yet.

// JSON(B) => boost::json::value
template <>
struct parse_field_traits<boost::json::value>
{
    static inline std::error_code is_compatible(std::int32_t type_oid)
    {
        return (type_oid == detail::json_oid || type_oid == detail::jsonb_oid)
                   ? std::error_code()
                   : client_errc::incompatible_field_type;
    }

    static inline std::error_code parse(
        field_view from,
        std::int32_t type_oid,
        protocol::format_code code,
        boost::json::value& to
    )
    {
        if (from.is_null())
            return client_errc::unexpected_null;
        BOOST_ASSERT(type_oid == detail::json_oid || type_oid == detail::jsonb_oid);

        // Both json and jsonb use plain JSON as their text representation. The same holds
        // for the binary representation of json, while binary jsonb has a version prefix
        if (code == protocol::format_code::binary && type_oid == detail::jsonb_oid)
            return types::parse_binary_jsonb(from, to);
        return types::parse_json(from.data_str(), to);
    }
};

}  // namespace nativepg

#endif  // NATIVEPG_DETAIL_FIELD_TRAITS_JSON_HPP
