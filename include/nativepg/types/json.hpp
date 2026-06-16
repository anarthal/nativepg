
#ifndef NATIVEPG_TYPES_JSON_HPP
#define NATIVEPG_TYPES_JSON_HPP

#include <boost/json/value.hpp>
#include <boost/json/parse.hpp>
#include <boost/system/error_code.hpp>

#include <span>
#include <string_view>

#include "nativepg/client_errc.hpp"

namespace nativepg::types {

using boost::system::error_code;


/*
| Type        | Category       | OID  | Storage size        |
|-------------|----------------|------|---------------------|
| json        | JSON Structure | 114  | Up to 1GB per field |
| jsonb       | JSON Structure | 3802 | Up to 255 per field |
| jsonpath    | USD            | 1266 | Up to 1GB per field |
 */

// Type mapping & wrapping
class pg_json
{
private:
    boost::json::value storage_;

public:
    // Default constructor: produces a null JSON value, must be implicit
    // so that `pg_json row{}` works in generic row-init code.
    pg_json() = default;

    // Explicit conversion from any boost::json::value-compatible argument
    template <typename Arg, typename... Args>
    explicit pg_json(Arg&& arg, Args&&... args)
        : storage_(std::forward<Arg>(arg), std::forward<Args>(args)...) {}

    bool has_key(std::string_view key) const {
        if (!storage_.is_object()) return false;
        return storage_.as_object().contains(key);
    }

    const boost::json::value& get() const { return storage_; }
    boost::json::value& get() { return storage_; }
};

class pg_jsonb
{
private:
    boost::json::value storage_;

public:
    // Default constructor: produces a null JSON value, must be implicit
    // so that `pg_jsonb row{}` works in generic row-init code.
    pg_jsonb() = default;

    // Explicit conversion from any boost::json::value-compatible argument
    template <typename Arg, typename... Args>
    explicit pg_jsonb(Arg&& arg, Args&&... args)
        : storage_(std::forward<Arg>(arg), std::forward<Args>(args)...) {}

    bool has_key(std::string_view key) const {
        if (!storage_.is_object()) return false;
        return storage_.as_object().contains(key);
    }

    const boost::json::value& get() const { return storage_; }
    boost::json::value& get() { return storage_; }
};


// JSON => types::pg_json (TEXT)
template <class T = types::pg_json>
constexpr error_code parse_text_json(std::span<const unsigned char> from, T& to) noexcept
{
    error_code ec{};

    // Construct a string_view pointing directly to the network buffer
    std::string_view json_text{reinterpret_cast<const char*>(from.data()), from.size()};

    // Parse the string into the Boost.JSON container
    if (!json_text.empty())
    {
        try
        {
            to = pg_json(boost::json::parse(json_text, ec));
        }
        catch (...)
        {
            ec = client_errc::protocol_value_error;
        }
    }

    return ec;
}

// JSON => types::pg_json (BINARY)
template <class T = types::pg_json>
constexpr error_code parse_binary_json(std::span<const unsigned char> from, T& to) noexcept
{
    error_code ec{};

    // Construct a string_view pointing directly to the network buffer
    std::string_view json_text{reinterpret_cast<const char*>(from.data()), from.size()};

    // Parse the string into the Boost.JSON container
    if (!json_text.empty())
    {
        try
        {
            to = pg_json(boost::json::parse(json_text, ec));
        }
        catch (...)
        {
            ec = client_errc::protocol_value_error;
        }
    }

    return ec;
}

// JSONB => types::pg_jsonb (TEXT)
template <class T = types::pg_jsonb>
constexpr error_code parse_text_jsonb(std::span<const unsigned char> from, T& to) noexcept
{
    error_code ec{};

    // Construct a string_view pointing directly to the network buffer
    std::string_view json_text{reinterpret_cast<const char*>(from.data()), from.size()};

    // Parse the string into the Boost.JSON container
    if (!json_text.empty())
    {
        try
        {
            to = pg_jsonb(boost::json::parse(json_text, ec));
        }
        catch (...)
        {
            ec = client_errc::protocol_value_error;
        }
    }

    return ec;
}

// JSONB => types::pg_jsonb (BINARY)
template <class T = types::pg_jsonb>
constexpr error_code parse_binary_jsonb(std::span<const unsigned char> from, T& to) noexcept
{
    error_code ec{};

    // Parse the bytes into the wrapped Boost.JSON container
    try
    {
        uint8_t version = from[0];
        if (version != 1)
            ec = client_errc::protocol_value_error;

        // Construct a string_view pointing directly to the network buffer
        std::string_view json_text{reinterpret_cast<const char*>(from.subspan(1).data()), from.size()-1};

        to = types::pg_jsonb(boost::json::parse(json_text, ec));
    }
    catch (...)
    {
        return client_errc::protocol_value_error;
    }

    return {};
}


}

#endif  // NATIVEPG_TYPES_JSON_HPP
