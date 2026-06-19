//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_DETAIL_FIELD_TRAITS_HPP
#define NATIVEPG_DETAIL_FIELD_TRAITS_HPP

#include <boost/multiprecision/cpp_dec_float.hpp>
#include <boost/system/error_code.hpp>

#include <concepts>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "nativepg/client_errc.hpp"
#include "nativepg/protocol/describe.hpp"

// TODO: at some point we will need to expose some customization
namespace nativepg {

namespace types {

typedef bool pg_bool;
typedef std::vector<std::byte> pg_bytea;
typedef std::int16_t pg_int2;
typedef std::int32_t pg_int4;
typedef std::int64_t pg_int8;
typedef float pg_float4;
typedef double pg_float8;
typedef boost::multiprecision::cpp_dec_float_50 pg_numeric;
typedef std::string_view pg_text;
typedef std::string_view pg_varchar;

}

namespace detail {

inline constexpr std::int32_t bool_oid = 16;

inline constexpr std::int32_t bytea_oid = 17;

inline constexpr std::int32_t int2_oid = 21;
inline constexpr std::int32_t int4_oid = 23;
inline constexpr std::int32_t int8_oid = 20;

inline constexpr std::int32_t float4_oid = 700;
inline constexpr std::int32_t float8_oid = 701;
inline constexpr std::int32_t numeric_oid = 1700;

inline constexpr std::int32_t text_oid = 25;
inline constexpr std::int32_t varchar_oid = 1043;


// --- Is a type compatible with what we get from DB?
// TODO: string diagnostics
template <class T>
struct field_is_compatible;

template <>
struct field_is_compatible<types::pg_bool>
{
    static inline boost::system::error_code call(const protocol::field_description& desc)
    {
        return desc.type_oid == bool_oid ? boost::system::error_code() : client_errc::incompatible_field_type;
    }
};

template <>
struct field_is_compatible<types::pg_bytea>
{
    static inline boost::system::error_code call(const protocol::field_description& desc)
    {
        return desc.type_oid == bytea_oid ? boost::system::error_code() : client_errc::incompatible_field_type;
    }
};

template <>
struct field_is_compatible<std::int16_t>
{
    static inline boost::system::error_code call(const protocol::field_description& desc)
    {
        return desc.type_oid == int2_oid ? boost::system::error_code() : client_errc::incompatible_field_type;
    }
};

template <>
struct field_is_compatible<std::int32_t>
{
    static inline boost::system::error_code call(const protocol::field_description& desc)
    {
        return desc.type_oid == int2_oid || desc.type_oid == int4_oid ? boost::system::error_code()
                                                                      : client_errc::incompatible_field_type;
    }
};

template <>
struct field_is_compatible<std::int64_t>
{
    static inline boost::system::error_code call(const protocol::field_description& desc)
    {
        return desc.type_oid == int2_oid || desc.type_oid == int4_oid || desc.type_oid == int8_oid
                   ? boost::system::error_code()
                   : client_errc::incompatible_field_type;
    }
};

template <>
struct field_is_compatible<types::pg_float4>
{
    static inline boost::system::error_code call(const protocol::field_description& desc)
    {
        return desc.type_oid == float4_oid ? boost::system::error_code() : client_errc::incompatible_field_type;
    }
};

template <>
struct field_is_compatible<types::pg_float8>
{
    static inline boost::system::error_code call(const protocol::field_description& desc)
    {
        return desc.type_oid == float8_oid ? boost::system::error_code() : client_errc::incompatible_field_type;
    }
};

template <>
struct field_is_compatible<types::pg_numeric>
{
    static inline boost::system::error_code call(const protocol::field_description& desc)
    {
        return desc.type_oid == numeric_oid ? boost::system::error_code() : client_errc::incompatible_field_type;
    }
};

template <>
struct field_is_compatible<types::pg_text>
{
    static inline boost::system::error_code call(const protocol::field_description& desc)
    {
        return (desc.type_oid == text_oid || desc.type_oid == varchar_oid) ? boost::system::error_code() : client_errc::incompatible_field_type;
    }
};


// Strings can be used to parse any type
template <class T>
    requires std::assignable_from<T&, std::string_view>
struct field_is_compatible<T>
{
    static inline boost::system::error_code call(const protocol::field_description&)
    {
        return boost::system::error_code();
    }
};

// --- Parse
template <class T>
struct field_parse;

template <>
struct field_parse<bool>
{
    static boost::system::error_code call(
        std::optional<std::span<const unsigned char>> from,
        const protocol::field_description& desc,
        bool& to
    );
};

template <>
struct field_parse<types::pg_bytea>
{
    static boost::system::error_code call(
        std::optional<std::span<const unsigned char>> from,
        const protocol::field_description& desc,
        bool& to
    );
};

template <>
struct field_parse<std::int16_t>
{
    static boost::system::error_code call(
        std::optional<std::span<const unsigned char>> from,
        const protocol::field_description& desc,
        std::int16_t& to
    );
};

template <>
struct field_parse<std::int32_t>
{
    static boost::system::error_code call(
        std::optional<std::span<const unsigned char>> from,
        const protocol::field_description& desc,
        std::int32_t& to
    );
};

template <>
struct field_parse<std::int64_t>
{
    static boost::system::error_code call(
        std::optional<std::span<const unsigned char>> from,
        const protocol::field_description& desc,
        std::int64_t& to
    );
};

template <>
struct field_parse<types::pg_float4>
{
    static boost::system::error_code call(
        std::optional<std::span<const unsigned char>> from,
        const protocol::field_description& desc,
        float& to
    );
};

template <>
struct field_parse<types::pg_float8>
{
    static boost::system::error_code call(
        std::optional<std::span<const unsigned char>> from,
        const protocol::field_description& desc,
        double& to
    );
};

template <>
struct field_parse<types::pg_numeric>
{
    static boost::system::error_code call(
        std::optional<std::span<const unsigned char>> from,
        const protocol::field_description& desc,
        types::pg_numeric& to
    );
};

template <class T>
    requires std::assignable_from<T&, std::string_view>
struct field_parse<T>
{
    static inline boost::system::error_code call(
        std::optional<boost::span<const unsigned char>> from,
        const protocol::field_description&,
        T& to
    )
    {
        if (!from.has_value())
            return client_errc::unexpected_null;
        auto data = *from;
        to = std::string_view(reinterpret_cast<const char*>(data.data()), data.size());
        return {};
    }
};


}
}// namespace nativepg::detail

#include "field_traits_datetime.hpp"

#endif
