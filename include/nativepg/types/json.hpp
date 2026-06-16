
#ifndef NATIVEPG_TYPES_JSON_HPP
#define NATIVEPG_TYPES_JSON_HPP

#include <boost/json/value.hpp>
#include <boost/json/parse.hpp>
#include <boost/endian/conversion.hpp>
#include <boost/system/error_code.hpp>

#include <charconv>
#include <chrono>
#include <cctype>
#include <cstring>
#include <span>
#include <string_view>
#include <system_error>

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



namespace detail {

// Helper to handle network byte order (Big-Endian) to Host byte order
inline uint32_t consume_uint32(std::span<const unsigned char>& bytes) {
    if (bytes.size() < 4) throw std::runtime_error("Unexpected EOF parsing JSONB header");
    uint32_t val;
    std::memcpy(&val, bytes.data(), 4);
    bytes = bytes.subspan(4);
    if constexpr (std::endian::native == std::endian::little) {
        boost::endian::endian_reverse_inplace(val);
    }
    return val;
}

// Forward declaration for recursion
boost::json::value parse_container(std::span<const unsigned char>& jentries,
                                   std::string_view data_pool,
                                   uint32_t header);

inline boost::json::value parse_jsonb_value(std::span<const unsigned char> wire_bytes) {
    if (wire_bytes.empty()) return nullptr;

    // 1. Validate version byte
    uint8_t version = wire_bytes[0];
    if (version != 1) throw std::runtime_error("Unsupported JSONB format version");

    std::span<const unsigned char> working_buffer = wire_bytes.subspan(1);

    // 2. Read main root container header
    uint32_t root_header = consume_uint32(working_buffer);
    uint32_t type_flag = root_header & 0xF0000000;
    uint32_t count = root_header & 0x0FFFFFFF;

    if (type_flag == 0x10000000) { // JB_FSCALAR
        // A scalar is represented as a single item disguised inside a container array
        type_flag = 0x20000000; // Force array processing logic
        count = 1;
    }

    // 3. Compute structural slicing boundaries
    size_t num_jentries = (type_flag == 0x40000000) ? (count * 2) : count; // Objects have pairs (key + val)
    size_t jentries_bytes_size = num_jentries * 4;

    if (working_buffer.size() < jentries_bytes_size) {
        throw std::runtime_error("Malformed JSONB packet: Truncated JEntries");
    }

    std::span<const unsigned char> jentries = working_buffer.subspan(0, jentries_bytes_size);
    std::span<const unsigned char> raw_data_pool = working_buffer.subspan(jentries_bytes_size);

    std::string_view data_pool(reinterpret_cast<const char*>(raw_data_pool.data()), raw_data_pool.size());

    // 4. Process layout
    return parse_container(jentries, data_pool, root_header);
}

inline boost::json::value parse_container(std::span<const unsigned char>& jentries,
                                          std::string_view data_pool,
                                          uint32_t header) {
    uint32_t type_flag = header & 0xF0000000;
    uint32_t count = header & 0x0FFFFFFF;

    size_t last_offset = 0;

    auto read_next_item = [&](std::span<const unsigned char>& je_span) -> boost::json::value {
        uint32_t jentry = consume_uint32(je_span);
        uint32_t entry_type = jentry & 0xF0000000;
        size_t end_offset = jentry & 0x0FFFFFFF;
        size_t length = end_offset - last_offset;

        std::string_view item_data = data_pool.substr(last_offset, length);
        last_offset = end_offset; // Step forward

        switch (entry_type) {
            case 0x00000000: // JB_STRING
                return boost::json::string(item_data);
            case 0x20000000: // JB_TRUE
                return true;
            case 0x30000000: // JB_FALSE
                return false;
            case 0x40000000: // JB_NULL
                return nullptr;
            case 0x10000000: { // JB_NUMERIC
                // PostgreSQL stores numbers in a proprietary struct (NumericVar).
                // Simplest catch: parse numeric string representation from postgres string conversion
                // Or fallback to processing native floats if precision isn't critical.
                return std::stod(std::string(item_data));
            }
            case 0x50000000: { // JB_NESTED_CONTAINER
                // Recursively parse inner object/array maps
                std::span<const unsigned char> nested_jentries = je_span;
                uint32_t nested_header = *reinterpret_cast<const uint32_t*>(item_data.data());
                if constexpr (std::endian::native == std::endian::little) {
                    boost::endian::endian_reverse_inplace(nested_header);
                }
                return parse_container(nested_jentries, item_data.substr(4), nested_header);
            }
            default:
                return nullptr;
        }
    };

    if (type_flag == 0x40000000) { // Is JSON Object
        boost::json::object obj;
        std::vector<boost::json::string> keys;
        keys.reserve(count);

        // Read all object keys first (Postgres bunches all keys sequentially)
        for (uint32_t i = 0; i < count; ++i) {
            keys.push_back(read_next_item(jentries).as_string());
        }
        // Read corresponding values right after keys
        for (uint32_t i = 0; i < count; ++i) {
            obj[keys[i]] = read_next_item(jentries);
        }
        return obj;
    }
    else { // Is JSON Array / Scalar
        boost::json::array arr;
        arr.reserve(count);
        for (uint32_t i = 0; i < count; ++i) {
            arr.push_back(read_next_item(jentries));
        }
        // Unwrap scalar if it's acting as root
        if ((header & 0xF0000000) == 0x10000000 && !arr.empty()) {
            return arr[0];
        }
        return arr;
    }
}
}  // namespace detail



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
    // Parse the bytes into the wrapped Boost.JSON container
    try
    {
        to = types::pg_jsonb(detail::parse_jsonb_value(from));
    }
    catch (...)
    {
        return client_errc::protocol_value_error;
    }

    return {};
}


}

#endif  // NATIVEPG_TYPES_JSON_HPP
