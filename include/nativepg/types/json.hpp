//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_TYPES_JSON_HPP
#define NATIVEPG_TYPES_JSON_HPP

#include <boost/json/parse.hpp>
#include <boost/json/value.hpp>
#include <boost/system/error_code.hpp>

#include <string_view>

#include "nativepg/client_errc.hpp"
#include "nativepg/field_view.hpp"

namespace nativepg::types {

inline boost::system::error_code parse_json(const std::string_view& json_text, boost::json::value& to)
{
    if (json_text.empty())
        return {};
    boost::system::error_code ec{};
    to = boost::json::parse(json_text, ec);
    return ec;
}

// JSONB => boost::json::value (BINARY)
// PostgreSQL JSONB binary has a 1-byte version prefix (always 0x01), followed by JSON text.
inline boost::system::error_code parse_binary_jsonb(const field_view& from, boost::json::value& to)
{
    const auto bytes = from.data();
    if (bytes.empty() || bytes[0] != 1)
        return client_errc::protocol_value_error;
    return parse_json(std::string_view{reinterpret_cast<const char*>(bytes.data() + 1), bytes.size() - 1}, to);
}

}  // namespace nativepg::types

// Registers field_is_compatible<T>/field_parse<T> for boost::json::value. Included here (rather than
// force-included by field_traits.hpp) so that only TUs opting into this header pay for it.
#include "nativepg/detail/field_traits_json.hpp"

#endif  // NATIVEPG_TYPES_JSON_HPP
