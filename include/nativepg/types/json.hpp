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

// JSON => boost::json::value (TEXT)
// PostgreSQL sends JSON as plain text — parse directly.
template <class T = boost::json::value>
boost::system::error_code parse_text_json(const field_view& from, T& to)
{
    std::string_view json_text = from.data_str();
    if (json_text.empty())
        return {};
    boost::system::error_code ec{};

    auto val = boost::json::parse(json_text, ec);

    if (!ec)
        to.swap(val);
    return ec;
}

// JSON => boost::json::value (BINARY)
// PostgreSQL binary JSON is identical wire format to text JSON.
template <class T = boost::json::value>
boost::system::error_code parse_binary_json(const field_view& from, T& to)
{
    return parse_text_json(from, to);
}

// JSONB => boost::json::value (TEXT)
template <class T = boost::json::value>
boost::system::error_code parse_text_jsonb(const field_view& from, T& to)
{
    std::string_view json_text = from.data_str();
    if (json_text.empty())
        return {};

    boost::system::error_code ec{};

    auto val = boost::json::parse(json_text, ec);
    if (!ec)
        to.swap(val);
    return ec;
}

// JSONB => boost::json::value (BINARY)
// PostgreSQL JSONB binary has a 1-byte version prefix (always 0x01), followed by JSON text.
template <class T = boost::json::value>
boost::system::error_code parse_binary_jsonb(const field_view& from, T& to)
{
    if (from.is_null())
        return client_errc::protocol_value_error;
    const auto bytes = from.data();
    // cppcheck-suppress knownConditionTrueFalse ; false positive: cppcheck's value-flow analysis assumes
    // a fixed length for this template's field_view parameter, but at runtime bytes can genuinely be
    // empty (a zero-length, non-NULL field) and this check guards against that case.
    if (bytes.empty() || bytes[0] != 1)
        return client_errc::protocol_value_error;
    return parse_text_jsonb(field_view{bytes.subspan(1)}, to);
}

}  // namespace nativepg::types

// Registers field_is_compatible<T>/field_parse<T> for boost::json::value. Included here (rather than
// force-included by field_traits.hpp) so that only TUs opting into this header pay for it.
#include "nativepg/detail/field_traits_json.hpp"

#endif  // NATIVEPG_TYPES_JSON_HPP
