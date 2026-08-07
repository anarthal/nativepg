//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_DETAIL_FIELD_TRAITS_DATETIME_HPP
#define NATIVEPG_DETAIL_FIELD_TRAITS_DATETIME_HPP

#pragma once

#include <boost/assert.hpp>

#include <chrono>
#include <cstdint>
#include <system_error>

#include "nativepg/client_errc.hpp"
#include "nativepg/field_traits.hpp"
#include "nativepg/field_view.hpp"
#include "nativepg/protocol/describe.hpp"
#include "nativepg/types.hpp"

namespace nativepg::detail {

inline constexpr std::int32_t date_oid = 1082;
inline constexpr std::int32_t time_oid = 1083;
inline constexpr std::int32_t timetz_oid = 1266;
inline constexpr std::int32_t timestamp_oid = 1114;
inline constexpr std::int32_t timestamptz_oid = 1184;
inline constexpr std::int32_t interval_oid = 1186;
inline constexpr std::int32_t tsrange_oid = 3908;
inline constexpr std::int32_t tstzrange_oid = 3910;
inline constexpr std::int32_t daterange_oid = 3912;

}  // namespace nativepg::detail

namespace nativepg {

// --- Parse
// There is no serialization counterpart yet: nativepg/types/datetime.hpp implements
// parsing only.

// DATE
template <>
struct parse_field_traits<std::chrono::sys_days>
{
    static std::error_code is_compatible(const protocol::field_description& desc)
    {
        return desc.type_oid == detail::date_oid ? std::error_code() : client_errc::incompatible_field_type;
    }

    static std::error_code parse(
        field_view from,
        const protocol::field_description& desc,
        std::chrono::sys_days& to
    )
    {
        if (from.is_null())
            return client_errc::unexpected_null;
        BOOST_ASSERT(desc.type_oid == detail::date_oid);
        return desc.fmt_code == protocol::format_code::text ? types::parse_text_date(from.data(), to)
                                                            : types::parse_binary_date(from.data(), to);
    }
};

// TIME
template <>
struct parse_field_traits<std::chrono::microseconds>
{
    static std::error_code is_compatible(const protocol::field_description& desc)
    {
        return desc.type_oid == detail::time_oid ? std::error_code() : client_errc::incompatible_field_type;
    }

    static std::error_code parse(
        field_view from,
        const protocol::field_description& desc,
        std::chrono::microseconds& to
    )
    {
        if (from.is_null())
            return client_errc::unexpected_null;
        BOOST_ASSERT(desc.type_oid == detail::time_oid);
        return desc.fmt_code == protocol::format_code::text ? types::parse_text_time(from.data(), to)
                                                            : types::parse_binary_time(from.data(), to);
    }
};

// TIMETZ
template <>
struct parse_field_traits<types::pg_timetz>
{
    static std::error_code is_compatible(const protocol::field_description& desc)
    {
        return desc.type_oid == detail::timetz_oid ? std::error_code() : client_errc::incompatible_field_type;
    }

    static std::error_code parse(
        field_view from,
        const protocol::field_description& desc,
        types::pg_timetz& to
    )
    {
        if (from.is_null())
            return client_errc::unexpected_null;
        BOOST_ASSERT(desc.type_oid == detail::timetz_oid);
        return desc.fmt_code == protocol::format_code::text ? types::parse_text_timetz(from.data(), to)
                                                            : types::parse_binary_timetz(from.data(), to);
    }
};

// TIMESTAMP
template <>
struct parse_field_traits<types::pg_timestamp>
{
    static std::error_code is_compatible(const protocol::field_description& desc)
    {
        return desc.type_oid == detail::timestamp_oid ? std::error_code()
                                                      : client_errc::incompatible_field_type;
    }

    static std::error_code parse(
        field_view from,
        const protocol::field_description& desc,
        types::pg_timestamp& to
    )
    {
        if (from.is_null())
            return client_errc::unexpected_null;
        BOOST_ASSERT(desc.type_oid == detail::timestamp_oid);
        return desc.fmt_code == protocol::format_code::text ? types::parse_text_timestamp(from.data(), to)
                                                            : types::parse_binary_timestamp(from.data(), to);
    }
};

// TIMESTAMPTZ
template <>
struct parse_field_traits<types::pg_timestamptz>
{
    static std::error_code is_compatible(const protocol::field_description& desc)
    {
        return desc.type_oid == detail::timestamptz_oid ? std::error_code()
                                                        : client_errc::incompatible_field_type;
    }

    static std::error_code parse(
        field_view from,
        const protocol::field_description& desc,
        types::pg_timestamptz& to
    )
    {
        if (from.is_null())
            return client_errc::unexpected_null;
        BOOST_ASSERT(desc.type_oid == detail::timestamptz_oid);
        return desc.fmt_code == protocol::format_code::text
                   ? types::parse_text_timestamptz(from.data(), to)
                   : types::parse_binary_timestamptz(from.data(), to);
    }
};

// INTERVAL
template <>
struct parse_field_traits<types::pg_interval>
{
    static std::error_code is_compatible(const protocol::field_description& desc)
    {
        return desc.type_oid == detail::interval_oid ? std::error_code()
                                                     : client_errc::incompatible_field_type;
    }

    static std::error_code parse(
        field_view from,
        const protocol::field_description& desc,
        types::pg_interval& to
    )
    {
        if (from.is_null())
            return client_errc::unexpected_null;
        BOOST_ASSERT(desc.type_oid == detail::interval_oid);
        return desc.fmt_code == protocol::format_code::text ? types::parse_text_interval(from.data(), to)
                                                            : types::parse_binary_interval(from.data(), to);
    }
};

}  // namespace nativepg

#endif  // NATIVEPG_DETAIL_FIELD_TRAITS_DATETIME_HPP
