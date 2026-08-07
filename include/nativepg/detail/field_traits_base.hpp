//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_DETAIL_FIELD_TRAITS_BASE_HPP
#define NATIVEPG_DETAIL_FIELD_TRAITS_BASE_HPP

#pragma once
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "nativepg/client_errc.hpp"
#include "nativepg/field_traits.hpp"
#include "nativepg/field_view.hpp"
#include "nativepg/types/base.hpp"

namespace nativepg::detail {

inline constexpr std::int32_t bool_oid = 16;

inline constexpr std::int32_t char_oid = 18;

inline constexpr std::int32_t bytea_oid = 17;

inline constexpr std::int32_t int2_oid = 21;
inline constexpr std::int32_t int4_oid = 23;
inline constexpr std::int32_t int8_oid = 20;

inline constexpr std::int32_t float4_oid = 700;
inline constexpr std::int32_t float8_oid = 701;

inline constexpr std::int32_t name_oid = 19;

inline constexpr std::int32_t oid_oid = 26;

inline constexpr std::int32_t text_oid = 25;
inline constexpr std::int32_t bpchar_oid = 1042;
inline constexpr std::int32_t varchar_oid = 1043;

// Any of the OIDs that we accept when parsing a string
inline constexpr bool is_string_oid(std::int32_t type_oid)
{
    return type_oid == text_oid || type_oid == varchar_oid || type_oid == name_oid || type_oid == bpchar_oid;
}

}  // namespace nativepg::detail

namespace nativepg {

// --- Parse

// BOOL
template <>
struct parse_field_traits<bool>
{
    static std::error_code is_compatible(std::int32_t type_oid)
    {
        if (type_oid == detail::bool_oid)
            return std::error_code{};

        return client_errc::incompatible_field_type;
    }

    static std::error_code parse_text(field_view from, [[maybe_unused]] std::int32_t type_oid, bool& to)
    {
        if (from.is_null())
            return client_errc::unexpected_null;
        BOOST_ASSERT(type_oid == detail::bool_oid);
        return types::parse_text_bool(from, to);
    }

    static std::error_code parse_binary(field_view from, [[maybe_unused]] std::int32_t type_oid, bool& to)
    {
        if (from.is_null())
            return client_errc::unexpected_null;
        BOOST_ASSERT(type_oid == detail::bool_oid);
        return types::parse_binary_bool(from, to);
    }
};

// BYTEA
template <>
struct parse_field_traits<std::vector<std::byte>>
{
    static std::error_code is_compatible(std::int32_t type_oid)
    {
        if (type_oid == detail::bytea_oid)
            return std::error_code{};

        return client_errc::incompatible_field_type;
    }

    static std::error_code parse_text(
        field_view from,
        [[maybe_unused]] std::int32_t type_oid,
        std::vector<std::byte>& to
    )
    {
        if (from.is_null())
            return client_errc::unexpected_null;
        BOOST_ASSERT(type_oid == detail::bytea_oid);
        return types::parse_text_bytea(from, to);
    }

    static std::error_code parse_binary(
        field_view from,
        [[maybe_unused]] std::int32_t type_oid,
        std::vector<std::byte>& to
    )
    {
        if (from.is_null())
            return client_errc::unexpected_null;
        BOOST_ASSERT(type_oid == detail::bytea_oid);
        return types::parse_binary_bytea(from, to);
    }
};

// INTERNAL CHAR "..." (double quoted string. Not CHAR(n) / CHARACTER(N)
// Is single byte so no UNICODE / UTF-8 support.
template <>
struct parse_field_traits<char>
{
    static std::error_code is_compatible(std::int32_t type_oid)
    {
        if (type_oid == detail::char_oid)
            return std::error_code{};

        return client_errc::incompatible_field_type;
    }

    static std::error_code parse_text(field_view from, [[maybe_unused]] std::int32_t type_oid, char& to)
    {
        if (from.is_null())
            return client_errc::unexpected_null;
        BOOST_ASSERT(type_oid == detail::char_oid);
        return types::parse_text_char(from, to);
    }

    static std::error_code parse_binary(field_view from, [[maybe_unused]] std::int32_t type_oid, char& to)
    {
        if (from.is_null())
            return client_errc::unexpected_null;
        BOOST_ASSERT(type_oid == detail::char_oid);
        return types::parse_binary_char(from, to);
    }
};

// INT2
template <>
struct parse_field_traits<std::int16_t>
{
    static std::error_code is_compatible(std::int32_t type_oid)
    {
        if (type_oid == detail::int2_oid)
            return std::error_code{};

        return client_errc::incompatible_field_type;
    }

    static std::error_code parse_text(
        field_view from,
        [[maybe_unused]] std::int32_t type_oid,
        std::int16_t& to
    )
    {
        if (from.is_null())
            return client_errc::unexpected_null;
        BOOST_ASSERT(type_oid == detail::int2_oid);
        return types::parse_text_int(from, to);
    }

    static std::error_code parse_binary(
        field_view from,
        [[maybe_unused]] std::int32_t type_oid,
        std::int16_t& to
    )
    {
        if (from.is_null())
            return client_errc::unexpected_null;
        BOOST_ASSERT(type_oid == detail::int2_oid);
        return types::parse_binary_int(from, to);
    }
};

// INT4. Widening from INT2 is allowed
template <>
struct parse_field_traits<std::int32_t>
{
    static std::error_code is_compatible(std::int32_t type_oid)
    {
        if (type_oid == detail::int4_oid || type_oid == detail::int2_oid)
            return std::error_code{};

        return client_errc::incompatible_field_type;
    }

    static std::error_code parse_text(field_view from, std::int32_t type_oid, std::int32_t& to)
    {
        if (from.is_null())
            return client_errc::unexpected_null;
        switch (type_oid)
        {
            case detail::int2_oid:
            {
                std::int16_t value{};
                const auto ec = types::parse_text_int(from, value);
                to = value;
                return ec;
            }
            case detail::int4_oid: return types::parse_text_int(from, to);

            default: BOOST_ASSERT(false); return {client_errc::incompatible_field_type};
        }
    }

    static std::error_code parse_binary(field_view from, std::int32_t type_oid, std::int32_t& to)
    {
        if (from.is_null())
            return client_errc::unexpected_null;
        switch (type_oid)
        {
            case detail::int2_oid:
            {
                std::int16_t value{};
                const auto ec = types::parse_binary_int(from, value);
                to = value;
                return ec;
            }
            case detail::int4_oid: return types::parse_binary_int(from, to);

            default: BOOST_ASSERT(false); return {client_errc::incompatible_field_type};
        }
    }
};

// INT8. Widening from INT2 and INT4 is allowed
template <>
struct parse_field_traits<std::int64_t>
{
    static std::error_code is_compatible(std::int32_t type_oid)
    {
        if (type_oid == detail::int8_oid || type_oid == detail::int4_oid || type_oid == detail::int2_oid)
            return std::error_code{};

        return client_errc::incompatible_field_type;
    }

    static std::error_code parse_text(field_view from, std::int32_t type_oid, std::int64_t& to)
    {
        if (from.is_null())
            return client_errc::unexpected_null;
        switch (type_oid)
        {
            case detail::int2_oid:
            {
                std::int16_t value{};
                const auto ec = types::parse_text_int(from, value);
                to = value;
                return ec;
            }
            case detail::int4_oid:
            {
                std::int32_t value{};
                const auto ec = types::parse_text_int(from, value);
                to = value;
                return ec;
            }
            case detail::int8_oid: return types::parse_text_int(from, to);
            default: BOOST_ASSERT(false); return {client_errc::incompatible_field_type};
        }
    }

    static std::error_code parse_binary(field_view from, std::int32_t type_oid, std::int64_t& to)
    {
        if (from.is_null())
            return client_errc::unexpected_null;
        switch (type_oid)
        {
            case detail::int2_oid:
            {
                std::int16_t value{};
                const auto ec = types::parse_binary_int(from, value);
                to = value;
                return ec;
            }
            case detail::int4_oid:
            {
                std::int32_t value{};
                const auto ec = types::parse_binary_int(from, value);
                to = value;
                return ec;
            }
            case detail::int8_oid: return types::parse_binary_int(from, to);
            default: BOOST_ASSERT(false); return {client_errc::incompatible_field_type};
        }
    }
};

// FLOAT4
template <>
struct parse_field_traits<float>
{
    static std::error_code is_compatible(std::int32_t type_oid)
    {
        if (type_oid == detail::float4_oid)
            return std::error_code{};

        return client_errc::incompatible_field_type;
    }

    static std::error_code parse_text(field_view from, [[maybe_unused]] std::int32_t type_oid, float& to)
    {
        if (from.is_null())
            return client_errc::unexpected_null;
        BOOST_ASSERT(type_oid == detail::float4_oid);
        return types::parse_text_float<float>(from, to);
    }

    static std::error_code parse_binary(field_view from, [[maybe_unused]] std::int32_t type_oid, float& to)
    {
        if (from.is_null())
            return client_errc::unexpected_null;
        BOOST_ASSERT(type_oid == detail::float4_oid);
        return types::parse_binary_float<float>(from, to);
    }
};

// FLOAT8. Widening from FLOAT4 is allowed
template <>
struct parse_field_traits<double>
{
    static std::error_code is_compatible(std::int32_t type_oid)
    {
        if (type_oid == detail::float8_oid || type_oid == detail::float4_oid)
            return std::error_code{};

        return client_errc::incompatible_field_type;
    }

    static std::error_code parse_text(field_view from, [[maybe_unused]] std::int32_t type_oid, double& to)
    {
        if (from.is_null())
            return client_errc::unexpected_null;
        switch (type_oid)
        {
            case detail::float8_oid: return types::parse_text_float<double>(from, to);
            case detail::float4_oid:
            {
                float value{};
                const auto ec = types::parse_text_float<float>(from, value);
                to = value;
                return ec;
            }
            default: BOOST_ASSERT(false); return {client_errc::incompatible_field_type};
        }
    }

    static std::error_code parse_binary(field_view from, [[maybe_unused]] std::int32_t type_oid, double& to)
    {
        if (from.is_null())
            return client_errc::unexpected_null;
        switch (type_oid)
        {
            case detail::float8_oid: return types::parse_binary_float<double>(from, to);
            case detail::float4_oid:
            {
                float value{};
                const auto ec = types::parse_binary_float<float>(from, value);
                to = value;
                return ec;
            }
            default: BOOST_ASSERT(false); return {client_errc::incompatible_field_type};
        }
    }
};

// TEXT, VARCHAR, NAME, BPCHAR. When parsing, only owning strings are allowed
template <class Traits, class Alloc>
struct parse_field_traits<std::basic_string<char, Traits, Alloc>>
{
    static std::error_code is_compatible(std::int32_t type_oid)
    {
        if (detail::is_string_oid(type_oid))
            return std::error_code{};

        return client_errc::incompatible_field_type;
    }

    static std::error_code parse_text(
        field_view from,
        [[maybe_unused]] std::int32_t type_oid,
        std::basic_string<char, Traits, Alloc>& to
    )
    {
        if (from.is_null())
            return client_errc::unexpected_null;
        BOOST_ASSERT(detail::is_string_oid(type_oid));
        return types::parse_text_text(from, to);
    }

    static std::error_code parse_binary(
        field_view from,
        [[maybe_unused]] std::int32_t type_oid,
        std::basic_string<char, Traits, Alloc>& to
    )
    {
        if (from.is_null())
            return client_errc::unexpected_null;
        BOOST_ASSERT(detail::is_string_oid(type_oid));
        return types::parse_binary_text(from, to);
    }
};

// OID
template <>
struct parse_field_traits<std::uint32_t>
{
    static std::error_code is_compatible(std::int32_t type_oid)
    {
        if (type_oid == detail::oid_oid)
            return std::error_code{};

        return client_errc::incompatible_field_type;
    }

    static std::error_code parse_text(
        field_view from,
        [[maybe_unused]] std::int32_t type_oid,
        std::uint32_t& to
    )
    {
        if (from.is_null())
            return client_errc::unexpected_null;
        BOOST_ASSERT(type_oid == detail::oid_oid);
        return types::parse_text_oid(from, to);
    }

    static std::error_code parse_binary(
        field_view from,
        [[maybe_unused]] std::int32_t type_oid,
        std::uint32_t& to
    )
    {
        if (from.is_null())
            return client_errc::unexpected_null;
        BOOST_ASSERT(type_oid == detail::oid_oid);
        return types::parse_binary_oid(from, to);
    }
};

// --- Serialize
// Types without a serialization function in nativepg/types/base.hpp (bool, bytea, char,
// float4, float8) are intentionally left out: they can't be used as query parameters yet.

// INT2
template <>
struct serialize_field_traits<std::int16_t>
{
    static constexpr std::int32_t oid = detail::int2_oid;

    static std::error_code serialize_text(std::int16_t value, std::vector<unsigned char>& to)
    {
        return types::serialize_text_int(value, to);
    }

    static std::error_code serialize_binary(std::int16_t value, std::vector<unsigned char>& to)
    {
        return types::serialize_binary_int(value, to);
    }
};

// INT4
template <>
struct serialize_field_traits<std::int32_t>
{
    static constexpr std::int32_t oid = detail::int4_oid;

    static std::error_code serialize_text(std::int32_t value, std::vector<unsigned char>& to)
    {
        return types::serialize_text_int(value, to);
    }

    static std::error_code serialize_binary(std::int32_t value, std::vector<unsigned char>& to)
    {
        return types::serialize_binary_int(value, to);
    }
};

// INT8
template <>
struct serialize_field_traits<std::int64_t>
{
    static constexpr std::int32_t oid = detail::int8_oid;

    static std::error_code serialize_text(std::int64_t value, std::vector<unsigned char>& to)
    {
        return types::serialize_text_int(value, to);
    }

    static std::error_code serialize_binary(std::int64_t value, std::vector<unsigned char>& to)
    {
        return types::serialize_binary_int(value, to);
    }
};

// TEXT. Unlike parsing, anything convertible to std::string_view is considered a string
template <std::convertible_to<std::string_view> T>
struct serialize_field_traits<T>
{
    static constexpr std::int32_t oid = detail::text_oid;

    static std::error_code serialize_text(std::string_view value, std::vector<unsigned char>& to)
    {
        return types::serialize_text_text(value, to);
    }

    static std::error_code serialize_binary(std::string_view value, std::vector<unsigned char>& to)
    {
        return types::serialize_binary_text(value, to);
    }
};

}  // namespace nativepg

#endif
