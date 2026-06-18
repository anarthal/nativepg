
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
// Tags to distinguish json vs jsonb at the type level (same structure, different OIDs)
struct json_tag {};
struct jsonb_tag {};

template <typename Tag>
class basic_pg_json
{
    boost::json::value storage_;
public:
    basic_pg_json() = default;

    explicit basic_pg_json(boost::json::value val)
        : storage_(std::move(val)) {}

    bool has_key(std::string_view key) const {
        if (!storage_.is_object()) return false;
        return storage_.as_object().contains(key);
    }

    const boost::json::value& get() const { return storage_; }
    boost::json::value& get() { return storage_; }
};

using pg_json  = basic_pg_json<json_tag>;
using pg_jsonb = basic_pg_json<jsonb_tag>;


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
        to = T(std::move(val));
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