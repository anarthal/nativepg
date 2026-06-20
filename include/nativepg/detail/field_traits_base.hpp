//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_DETAIL_FIELD_TRAITS_BASE_HPP
#define NATIVEPG_DETAIL_FIELD_TRAITS_BASE_HPP

#include <boost/multiprecision/cpp_dec_float.hpp>
#include <boost/system/error_code.hpp>

#include <optional>
#include <span>
#include <string_view>

#include "nativepg/extended_error.hpp"
#include "nativepg/protocol/describe.hpp"
#include "nativepg/types/base.hpp"

// TODO: at some point we will need to expose some customization
namespace nativepg::detail {

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
    static inline boost::system::error_code call(
        const protocol::field_description& desc
    )
    {
        if (desc.type_oid == bool_oid)
            return boost::system::error_code{};
        /*
        diag = diagnostics{
            "Expected PostgreSQL bool (OID 16) but got OID " +
            std::to_string(desc.type_oid) +
            " for field '" + std::string(desc.name) + "'"
        };
        */
        return client_errc::incompatible_field_type;
    }
};

template <>
struct field_is_compatible<types::pg_bytea>
{
    static inline boost::system::error_code call(const protocol::field_description& desc)
    {
        if (desc.type_oid == bytea_oid)
            return  boost::system::error_code{};

        return client_errc::incompatible_field_type;
    }
};

template <>
struct field_is_compatible<std::int16_t>
{
    static inline boost::system::error_code call(const protocol::field_description& desc)
    {
        if (desc.type_oid == int2_oid)
            return boost::system::error_code{};

        return client_errc::incompatible_field_type;
    }
};

template <>
struct field_is_compatible<std::int32_t>
{
    static inline boost::system::error_code call(const protocol::field_description& desc)
    {
        if (desc.type_oid == int4_oid)
            return  boost::system::error_code{};

        return client_errc::incompatible_field_type;
    }
};

template <>
struct field_is_compatible<std::int64_t>
{
    static inline boost::system::error_code call(const protocol::field_description& desc)
    {
        if (desc.type_oid == int8_oid)
            return  boost::system::error_code{};

        return client_errc::incompatible_field_type;
    }
};

template <>
struct field_is_compatible<types::pg_float4>
{
    static inline boost::system::error_code call(const protocol::field_description& desc)
    {
        if (desc.type_oid == float4_oid)
            return boost::system::error_code{};

        return client_errc::incompatible_field_type;
    }
};

template <>
struct field_is_compatible<types::pg_float8>
{
    static inline boost::system::error_code call(const protocol::field_description& desc)
    {
        if (desc.type_oid == float8_oid)
            return  boost::system::error_code{};

        return client_errc::incompatible_field_type;
    }
};

template <>
struct field_is_compatible<types::pg_numeric>
{
    static inline boost::system::error_code call(const protocol::field_description& desc)
    {
        if (desc.type_oid == numeric_oid)
            return boost::system::error_code{};

        return client_errc::incompatible_field_type;
    }
};

template <>
struct field_is_compatible<types::pg_text>
{
    static inline boost::system::error_code call(const protocol::field_description& desc)
    {
       if (desc.type_oid == text_oid || desc.type_oid == varchar_oid)
           return boost::system::error_code{};

        return client_errc::incompatible_field_type;
    }
};

// --- Parse
template <class T>
struct field_parse;

template <>
struct field_parse<types::pg_bool>
{
    static inline boost::system::error_code call(
        std::optional<std::span<const unsigned char>> from,
        const protocol::field_description& desc,
        bool& to
    )
    {
        if (!from.has_value())
            return client_errc::unexpected_null;
        BOOST_ASSERT(desc.type_oid == bool_oid);
        return desc.fmt_code == protocol::format_code::text ? types::parse_text_bool(*from, to)
                                                        : types::parse_binary_bool(*from, to);
    }
};

template <>
struct field_parse<types::pg_bytea>
{
    static inline boost::system::error_code call(
        std::optional<std::span<const unsigned char>> from,
        const protocol::field_description& desc,
        types::pg_bytea& to
    )
    {
        if (!from.has_value())
            return client_errc::unexpected_null;
        BOOST_ASSERT(desc.type_oid == bytea_oid);
        return desc.fmt_code == protocol::format_code::text ? types::parse_text_bytea(*from, to)
                                                        : types::parse_binary_bytea(*from, to);
    }
};

template <>
struct field_parse<std::int16_t>
{
    static inline boost::system::error_code call(
        std::optional<std::span<const unsigned char>> from,
        const protocol::field_description& desc,
        std::int16_t& to
    )
    {
        if (!from.has_value())
            return client_errc::unexpected_null;
        BOOST_ASSERT(desc.type_oid == int2_oid || desc.type_oid == int4_oid);
        return desc.fmt_code == protocol::format_code::text ? types::parse_text_int(*from, to)
                                                            : types::parse_binary_int(*from, to);
    }
};

template <>
struct field_parse<std::int32_t>
{
    static inline boost::system::error_code call(
        std::optional<std::span<const unsigned char>> from,
        const protocol::field_description& desc,
        std::int32_t& to
    )
    {
        if (!from.has_value())
            return client_errc::unexpected_null;
        auto data = *from;
        switch (desc.type_oid)
        {
            case int2_oid:
            {
                std::int16_t value{};
                auto ec = desc.fmt_code == protocol::format_code::text ? types::parse_text_int(data, value)
                                                                       : types::parse_binary_int(data, value);
                to = value;
                return ec;
            }
            case int4_oid:
                return desc.fmt_code == protocol::format_code::text ? types::parse_text_int(data, to)
                                                                    : types::parse_binary_int(data, to);
            default: BOOST_ASSERT(false); return {};
        }
    }
};

template <>
struct field_parse<std::int64_t>
{
    static inline boost::system::error_code call(
        std::optional<std::span<const unsigned char>> from,
        const protocol::field_description& desc,
        std::int64_t& to
    )
    {
        if (!from.has_value())
            return client_errc::unexpected_null;
        auto data = *from;
        switch (desc.type_oid)
        {
            case int2_oid:
            {
                std::int16_t value{};
                auto ec = desc.fmt_code == protocol::format_code::text ? types::parse_text_int(data, value)
                                                                       : types::parse_binary_int(data, value);
                to = value;
                return ec;
            }
            case int4_oid:
            {
                std::int32_t value{};
                auto ec = desc.fmt_code == protocol::format_code::text ? types::parse_text_int(data, value)
                                                                       : types::parse_binary_int(data, value);
                to = value;
                return ec;
            }
            case int8_oid:
                return desc.fmt_code == protocol::format_code::text ? types::parse_text_int(data, to)
                                                                    : types::parse_binary_int(data, to);
            default: BOOST_ASSERT(false); return {};
        }
    }
};

template <>
struct field_parse<types::pg_float4>
{
    static inline boost::system::error_code call(
        std::optional<std::span<const unsigned char>> from,
        const protocol::field_description& desc,
        float& to
    )
    {
        if (!from.has_value())
            return client_errc::unexpected_null;
        BOOST_ASSERT(desc.type_oid == float4_oid);
        return desc.fmt_code == protocol::format_code::text ? types::parse_text_float4(*from, to)
                                                        : types::parse_binary_float4(*from, to);
    }
};

template <>
struct field_parse<types::pg_float8>
{
    static inline boost::system::error_code call(
        std::optional<std::span<const unsigned char>> from,
        const protocol::field_description& desc,
        double& to
    )
    {
        if (!from.has_value())
            return client_errc::unexpected_null;
        BOOST_ASSERT(desc.type_oid == float8_oid);
        return desc.fmt_code == protocol::format_code::text ? types::parse_text_float8(*from, to)
                                                        : types::parse_binary_float8(*from, to);
    }
};

template <>
struct field_parse<types::pg_numeric>
{
    static inline boost::system::error_code call(
        std::optional<std::span<const unsigned char>> from,
        const protocol::field_description& desc,
        types::pg_numeric& to
    )
    {
        if (!from.has_value())
            return client_errc::unexpected_null;
        BOOST_ASSERT(desc.type_oid == numeric_oid);
        return desc.fmt_code == protocol::format_code::text ? types::parse_text_numeric(*from, to)
                                                        : types::parse_binary_numeric(*from, to);
    }
};




}// namespace nativepg::detail

#endif
