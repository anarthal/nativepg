
#ifndef NATIVEPG_TYPES_JSON_HPP
#define NATIVEPG_TYPES_JSON_HPP

#include <boost/json/value.hpp>
#include <boost/json/parse.hpp>

#include <span>
#include <string_view>

#include "nativepg/client_errc.hpp"

namespace nativepg::types {

using boost::system::error_code;

// Type mapping & wrapping
typedef boost::json::value pg_json; // OID 114
typedef boost::json::value pg_jsonb; // OID 3802


// JSON => pg_json (TEXT)
// PostgreSQL sends JSON as plain text — parse directly.
template <class T = pg_json>
inline error_code parse_text_json(std::span<const unsigned char> from, T& to)
{
    std::string_view json_text{reinterpret_cast<const char*>(from.data()), from.size()};
    if (json_text.empty())
        return {};
    error_code ec{};

    auto val = boost::json::parse(json_text, ec);

    if (!ec)
        to.swap(val);
    return ec;
}

// JSON => pg_json (BINARY)
// PostgreSQL binary JSON is identical wire format to text JSON.
template <class T = pg_json>
inline error_code parse_binary_json(std::span<const unsigned char> from, T& to)
{
    return parse_text_json(from, to);
}

// JSONB => pg_jsonb (TEXT)
template <class T = pg_jsonb>
inline error_code parse_text_jsonb(std::span<const unsigned char> from, T& to)
{
    std::string_view json_text{reinterpret_cast<const char*>(from.data()), from.size()};
    if (json_text.empty())
        return {};

    error_code ec{};

    auto val = boost::json::parse(json_text, ec);
    if (!ec)
        to = T(std::move(val));
    return ec;
}

// JSONB => pg_jsonb (BINARY)
// PostgreSQL JSONB binary has a 1-byte version prefix (always 0x01), followed by JSON text.
template <class T = pg_jsonb>
inline error_code parse_binary_jsonb(std::span<const unsigned char> from, T& to)
{
    if (from.empty())
        return client_errc::protocol_value_error;
    if (from[0] != 1)
        return client_errc::protocol_value_error;
    return parse_text_jsonb(from.subspan(1), to);
}

}  // namespace nativepg::types

#endif  // NATIVEPG_TYPES_JSON_HPP