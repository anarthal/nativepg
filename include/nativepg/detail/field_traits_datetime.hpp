
#ifndef NATIVEPG_FIELD_TRAITS_DATETIME_HPP
#define NATIVEPG_FIELD_TRAITS_DATETIME_HPP

#include <chrono>

#include <boost/system/error_code.hpp>

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

template <class T>
struct field_is_compatible;

// DATE
template <>
struct field_is_compatible<std::chrono::sys_days>
{
    static inline boost::system::error_code call(const protocol::field_description& desc)
    {
        return desc.type_oid == date_oid ? boost::system::error_code() : client_errc::incompatible_field_type;
    }
};

// TIME
template <>
struct field_is_compatible<std::chrono::microseconds>
{
    static inline boost::system::error_code call(const protocol::field_description& desc)
    {
        return desc.type_oid == time_oid ? boost::system::error_code() : client_errc::incompatible_field_type;
    }
};

// TIMETZ
template <>
struct field_is_compatible<types::pg_timetz>
{
    static inline boost::system::error_code call(const protocol::field_description& desc)
    {
        return desc.type_oid == timetz_oid ? boost::system::error_code() : client_errc::incompatible_field_type;
    }
};

// TIMESTAMP
template <>
struct field_is_compatible<types::pg_timestamp>
{
    static inline boost::system::error_code call(const protocol::field_description& desc)
    {
        return desc.type_oid == timestamp_oid ? boost::system::error_code() : client_errc::incompatible_field_type;
    }
};

// TIMESTAMPTZ
template <>
struct field_is_compatible<types::pg_timestamptz>
{
    static inline boost::system::error_code call(const protocol::field_description& desc)
    {
        return desc.type_oid == timestamptz_oid ? boost::system::error_code() : client_errc::incompatible_field_type;
    }
};

// INTERVAL
template <>
struct field_is_compatible<types::pg_interval>
{
    static inline boost::system::error_code call(const protocol::field_description& desc)
    {
        return desc.type_oid == interval_oid ? boost::system::error_code() : client_errc::incompatible_field_type;
    }
};


template <class T>
struct field_parse;

// DATE
template <>
struct field_parse<std::chrono::sys_days>
{
    static inline boost::system::error_code call(
        const field_view& from,
        const protocol::field_description& desc,
        std::chrono::sys_days& to
    )
    {
        if (from.is_null())
            return client_errc::unexpected_null;
        BOOST_ASSERT(desc.type_oid == date_oid);
        return desc.fmt_code == protocol::format_code::text ? types::parse_text_date(from.data(), to)
                                                            : types::parse_binary_date(from.data(), to);
    }
};

// TIME
template <>
struct field_parse<std::chrono::microseconds>
{
    static inline boost::system::error_code call(
        const field_view& from,
        const protocol::field_description& desc,
        std::chrono::microseconds& to
    )
    {
        if (from.is_null())
            return client_errc::unexpected_null;
        BOOST_ASSERT(desc.type_oid == time_oid);
        return desc.fmt_code == protocol::format_code::text ? types::parse_text_time(from.data(), to)
                                                            : types::parse_binary_time(from.data(), to);
    }
};

// TIMETZ
template <>
struct field_parse<types::pg_timetz>
{
    static inline boost::system::error_code call(
        const field_view& from,
        const protocol::field_description& desc,
        types::pg_timetz& to
    )
    {
        if (from.is_null())
            return client_errc::unexpected_null;
        BOOST_ASSERT(desc.type_oid == timetz_oid);
        return desc.fmt_code == protocol::format_code::text ? parse_text_timetz(from.data(), to)
                                                            : parse_binary_timetz(from.data(), to);
    }
};

// TIMESTAMP
template <>
struct field_parse<types::pg_timestamp>
{
    static inline boost::system::error_code call(
        const field_view& from,
        const protocol::field_description& desc,
        types::pg_timestamp& to
    )
    {
        if (from.is_null())
            return client_errc::unexpected_null;
        BOOST_ASSERT(desc.type_oid == timestamp_oid);
        return desc.fmt_code == protocol::format_code::text ? types::parse_text_timestamp(from.data(), to)
                                                            : types::parse_binary_timestamp(from.data(), to);
    }
};

// TIMESTAMPTZ
template <>
struct field_parse<types::pg_timestamptz>
{
    static inline boost::system::error_code call(
        const field_view& from,
        const protocol::field_description& desc,
        types::pg_timestamptz& to
    )
    {
        if (from.is_null())
            return client_errc::unexpected_null;
        BOOST_ASSERT(desc.type_oid == timestamptz_oid);
        return desc.fmt_code == protocol::format_code::text ? types::parse_text_timestamptz(from.data(), to)
                                                            : types::parse_binary_timestamptz(from.data(), to);
    }
};

// INTERVAL
template <>
struct field_parse<types::pg_interval>
{
    static inline boost::system::error_code call(
        const field_view& from,
        const protocol::field_description& desc,
        types::pg_interval& to
    )
    {
        if (from.is_null())
            return client_errc::unexpected_null;
        BOOST_ASSERT(desc.type_oid == interval_oid);
        return desc.fmt_code == protocol::format_code::text ? parse_text_interval(from.data(), to)
                                                            : parse_binary_interval(from.data(), to);
    }
};

}
#endif  // NATIVEPG_FIELD_TRAITS_DATETIME_HPP
