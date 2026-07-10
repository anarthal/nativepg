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

inline constexpr std::int32_t text_oid = 25;
inline constexpr std::int32_t varchar_oid = 1043;


// --- Is a type compatible with what we get from DB?
// TODO: string diagnostics
template <class T>
struct field_is_compatible;

template <>
struct field_is_compatible<bool>
{
    static inline boost::system::error_code call(
        const protocol::field_description& desc
    )
    {
        if (desc.type_oid == bool_oid)
            return boost::system::error_code{};
        return client_errc::incompatible_field_type;
    }
};

template <>
struct field_is_compatible<std::vector<std::byte>>
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
        if (desc.type_oid == int4_oid || desc.type_oid == int2_oid)
            return  boost::system::error_code{};

        return client_errc::incompatible_field_type;
    }
};

template <>
struct field_is_compatible<std::int64_t>
{
    static inline boost::system::error_code call(const protocol::field_description& desc)
    {
        if (desc.type_oid == int8_oid || desc.type_oid == int4_oid || desc.type_oid == int2_oid)
            return  boost::system::error_code{};

        return client_errc::incompatible_field_type;
    }
};

template <>
struct field_is_compatible<float>
{
    static inline boost::system::error_code call(const protocol::field_description& desc)
    {
        if (desc.type_oid == float4_oid)
            return boost::system::error_code{};

        return client_errc::incompatible_field_type;
    }
};

template <>
struct field_is_compatible<double>
{
    static inline boost::system::error_code call(const protocol::field_description& desc)
    {
        if (desc.type_oid == float8_oid)
            return  boost::system::error_code{};

        return client_errc::incompatible_field_type;
    }
};

template <>
struct field_is_compatible<std::string>
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
struct field_parse<bool>
{
    static inline boost::system::error_code call(
        const field_view& from,
        const protocol::field_description& desc,
        bool& to
    )
    {
        if (from.is_null())
            return client_errc::unexpected_null;
        BOOST_ASSERT(desc.type_oid == bool_oid);
        return desc.fmt_code == protocol::format_code::text ? types::parse_text_bool(from.data(), to)
                                                        : types::parse_binary_bool(from.data(), to);
    }
};

template <>
struct field_parse<std::vector<std::byte>>
{
    static inline boost::system::error_code call(
        const field_view& from,
        const protocol::field_description& desc,
        std::vector<std::byte>& to
    )
    {
        if (from.is_null())
            return client_errc::unexpected_null;
        BOOST_ASSERT(desc.type_oid == bytea_oid);
        return desc.fmt_code == protocol::format_code::text ? types::parse_text_bytea(from.data(), to)
                                                        : types::parse_binary_bytea(from.data(), to);
    }
};

template <>
struct field_parse<std::int16_t>
{
    static inline boost::system::error_code call(
        const field_view& from,
        const protocol::field_description& desc,
        std::int16_t& to
    )
    {
        if (from.is_null())
            return client_errc::unexpected_null;
        BOOST_ASSERT(desc.type_oid == int2_oid || desc.type_oid == int4_oid);
        return desc.fmt_code == protocol::format_code::text ? types::parse_text_int(from.data(), to)
                                                            : types::parse_binary_int(from.data(), to);
    }
};

template <>
struct field_parse<std::int32_t>
{
    static inline boost::system::error_code call(
        const field_view& from,
        const protocol::field_description& desc,
        std::int32_t& to
    )
    {
        if (from.is_null())
            return client_errc::unexpected_null;
        const auto data = from.data();
        switch (desc.type_oid)
        {
            case int2_oid:
            {
                std::int16_t value{};
                const auto ec = desc.fmt_code == protocol::format_code::text ? types::parse_text_int(data, value)
                                                                       : types::parse_binary_int(data, value);
                to = value;
                return ec;
            }
            case int4_oid:
            {
                return desc.fmt_code == protocol::format_code::text ? types::parse_text_int(data, to)
                                                                    : types::parse_binary_int(data, to);
            }

            default: BOOST_ASSERT(false);
        }
    }
};

template <>
struct field_parse<std::int64_t>
{
    static inline boost::system::error_code call(
        const field_view& from,
        const protocol::field_description& desc,
        std::int64_t& to
    )
    {
        if (from.is_null())
            return client_errc::unexpected_null;
        const auto data = from.data();
        switch (desc.type_oid)
        {
            case int2_oid:
            {
                std::int16_t value{};
                const auto ec = desc.fmt_code == protocol::format_code::text ? types::parse_text_int(data, value)
                                                                       : types::parse_binary_int(data, value);
                to = value;
                return ec;
            }
            case int4_oid:
            {
                std::int32_t value{};
                const auto ec = desc.fmt_code == protocol::format_code::text ? types::parse_text_int(data, value)
                                                                       : types::parse_binary_int(data, value);
                to = value;
                return ec;
            }
            case int8_oid:
                return desc.fmt_code == protocol::format_code::text ? types::parse_text_int(data, to)
                                                                    : types::parse_binary_int(data, to);
            default: BOOST_ASSERT(false);
        }
    }
};

template <>
struct field_parse<float>
{
    static inline boost::system::error_code call(
        const field_view& from,
        const protocol::field_description& desc,
        float& to
    )
    {
        if (from.is_null())
            return client_errc::unexpected_null;
        BOOST_ASSERT(desc.type_oid == float4_oid);
        return desc.fmt_code == protocol::format_code::text ? types::parse_text_float<float>(from.data(), to)
                                                        : types::parse_binary_float<float>(from.data(), to);
    }
};

template <>
struct field_parse<double>
{
    static inline boost::system::error_code call(
        const field_view& from,
        const protocol::field_description& desc,
        double& to
    )
    {
        if (from.is_null())
            return client_errc::unexpected_null;
        BOOST_ASSERT(desc.type_oid == float8_oid);
        return desc.fmt_code == protocol::format_code::text ? types::parse_text_float<double>(from.data(), to)
                                                        : types::parse_binary_float<double>(from.data(), to);
    }
};


template <>
struct field_parse<std::string>
{
    static inline boost::system::error_code call(
        const field_view& from,
        const protocol::field_description& desc,
        std::string& to
    )
    {
        if (from.is_null())
            return client_errc::unexpected_null;
        BOOST_ASSERT(desc.type_oid == text_oid || desc.type_oid == varchar_oid);
        return desc.fmt_code == protocol::format_code::text ? types::parse_text_text(from.data(), to)
                                                        : types::parse_binary_text(from.data(), to);
    }
};




}// namespace nativepg::detail

#endif
