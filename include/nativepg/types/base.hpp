#ifndef NATIVEPG_TYPES_BASE_HPP
#define NATIVEPG_TYPES_BASE_HPP

#include <boost/endian/conversion.hpp>
#include <boost/system/error_code.hpp>

#include <charconv>
#include <format>
#include <span>
#include <vector>
#include <cstddef>


#include "nativepg/client_errc.hpp"

namespace nativepg {
namespace types {

using boost::system::error_code;


/*
 Type mapping
| Type      | Category | OID  | C++ type                                | Storage size                                                                |
|-----------|----------|------|-----------------------------------------|-----------------------------------------------------------------------------|
| bool      | base     | 16   | bool                                    | 1 byte                                                                      |
| bytea     | base     | 17   | std::vector<std::byte>>                 | variable (1 or 4 bytes header + actual binary data)                         |
| int2      | base     | 21   | std::int16_t                            | 2 bytes                                                                     |
| int4      | base     | 23   | std::int32_t                            | 4 bytes                                                                     |
| int8      | base     | 20   | std::int64_t                            | 8 bytes                                                                     |
| float4    | base     | 700  | float                                   | 4 bytes                                                                     |
| float8    | base     | 701  | double                                  | 8 bytes                                                                     |
| text      | base     | 25   | std::string                             | variable (1 or 4 bytes header + actual string data)                         |
| varchar   | base     | 1043 | std::string                             | variable (1 or 4 bytes header + actual string data)                         |
*/

constexpr std::size_t max_digits_text = 25;

namespace detail {

template <typename T>
constexpr error_code parse_text_to_number(const std::string& sv, T& to) {
    T result = 0;
    auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), result);
    if (ec == std::errc{} && ptr == sv.data() + sv.size())
        to = std::move(result); // Success
    else
        return client_errc::protocol_value_error;

    return {};
}

}


//BOOL => bool;
template <class T = bool>
constexpr  error_code parse_text_bool(const std::span<const unsigned char> from, T& to)
{
    if (const std::string_view sv{reinterpret_cast<const char*>(from.data()), from.size()};
        sv == "t" || sv == "true")
        to = true;
    else if (sv == "f" || sv == "false")
        to = false;
    else
        return client_errc::protocol_value_error;
    return {};
}

template <class T = bool>
constexpr error_code parse_binary_bool(const std::span<const unsigned char> from, T& to)
{
    if (from.size() != 1)
        return client_errc::protocol_value_error;
    to = from[0] != 0;
    return {};
}

//BYTEA => std::vector<std::byte>>;
template <class T = std::vector<std::byte>>
constexpr error_code parse_text_bytea(const std::span<const unsigned char> from, T& to)
{
    // PostgresSQL text format for bytea is \x followed by hex pairs
    std::string_view sv{reinterpret_cast<const char*>(from.data()), from.size()};
    if (sv.size() < 2 || sv[0] != '\\' || sv[1] != 'x')
        return client_errc::protocol_value_error;
    sv.remove_prefix(2);
    if (sv.size() % 2 != 0)
        return client_errc::protocol_value_error;
    to.clear();
    to.reserve(sv.size() / 2);
    for (std::size_t i = 0; i < sv.size(); i += 2)
    {
        unsigned char byte{};
        if (auto [ptr, ec] = std::from_chars(sv.data() + i, sv.data() + i + 2, byte, 16);
            ec != std::errc{} || ptr != sv.data() + i + 2)
            return client_errc::protocol_value_error;
        to.push_back(static_cast<std::byte>(byte));
    }
    return {};
}

template <class T = std::vector<std::byte>>
constexpr error_code parse_binary_bytea(const std::span<const unsigned char> from, T& to)
{
    // Binary format is raw bytes — copy directly
    to.clear();
    to.reserve(from.size());
    for (auto byte : from)
        to.push_back(static_cast<std::byte>(byte));
    return {};
}

// INT => std::int_t
template <class T>
constexpr error_code parse_text_int(const std::span<const unsigned char> from, T& to)
{
    if (from.size() > max_digits_text)
        return client_errc::protocol_value_error;

    const std::string sv{reinterpret_cast<const char*>(from.data()), from.size()};
    return detail::parse_text_to_number<T>(sv, to);
}

template <class T>
constexpr error_code parse_binary_int(const std::span<const unsigned char> from, T& to)
{
    if (from.size() != sizeof(T))
        return client_errc::protocol_value_error;
    to = boost::endian::endian_load<T, sizeof(T), boost::endian::order::big>(from.data());
    return {};
}


// FLOAT => float
template <class T>
constexpr error_code parse_text_float(const std::span<const unsigned char> from, T& to)
{
    if (from.size() > max_digits_text)
        return client_errc::protocol_value_error;

    const std::string s{reinterpret_cast<const char*>(from.data()), from.size()};
    return detail::parse_text_to_number<T>(s, to);
}

template <class T>
constexpr error_code parse_binary_float(const std::span<const unsigned char> from, T& to)
{
    if (from.size() != sizeof(T))
        return client_errc::protocol_value_error;

    to = boost::endian::endian_load<T, sizeof(T), boost::endian::order::big>(from.data());
    return {};
}





// TEXT | VARCHAR => std::string_view or std::string
template <class T>
constexpr error_code parse_text_text(const std::span<const unsigned char> from, T& to)
{
    to = T{reinterpret_cast<const char*>(from.data()), from.size()};
    return {};
}

template <class T>
constexpr error_code parse_binary_text(const std::span<const unsigned char> from, T& to)
{
    to = T{reinterpret_cast<const char*>(from.data()), from.size()};
    return {};
}

}

template <class T>
inline std::basic_ostream<T>& operator<<(std::basic_ostream<T>& os, const std::vector<std::byte>& bytes)
{
    os << "0x";
    for (auto byte : bytes)
        os << std::format("{:02x}", static_cast<int>(byte));
    return os;
}

}

#endif  // NATIVEPG_TYPES_BASE_HPP
