
#ifndef NATIVEPG_FIELD_TRAITS_DATETIME_HPP
#define NATIVEPG_FIELD_TRAITS_DATETIME_HPP

#include <chrono>

#include <boost/system/error_code.hpp>

#include "nativepg/types.hpp"

namespace nativepg::detail {

template <class T>
struct field_is_compatible;

// DATE
template <>
struct field_is_compatible<std::chrono::sys_days>
{
    static inline boost::system::error_code call(const protocol::field_description& desc)
    {
        return desc.type_oid == 1082 ? boost::system::error_code() : client_errc::incompatible_field_type;
    }
};

// TIME
template <>
struct field_is_compatible<std::chrono::microseconds>
{
    static inline boost::system::error_code call(const protocol::field_description& desc)
    {
        return desc.type_oid == 1083 ? boost::system::error_code() : client_errc::incompatible_field_type;
    }
};

// TIMETZ
template <>
struct field_is_compatible<types::pg_timetz>
{
    static inline boost::system::error_code call(const protocol::field_description& desc)
    {
        return desc.type_oid == 1266 ? boost::system::error_code() : client_errc::incompatible_field_type;
    }
};

// TIMESTAMP
template <>
struct field_is_compatible<types::pg_timestamp>
{
    static inline boost::system::error_code call(const protocol::field_description& desc)
    {
        return desc.type_oid == 1114 ? boost::system::error_code() : client_errc::incompatible_field_type;
    }
};

// TIMESTAMPTZ
template <>
struct field_is_compatible<types::pg_timestamptz>
{
    static inline boost::system::error_code call(const protocol::field_description& desc)
    {
        return desc.type_oid == 1184 ? boost::system::error_code() : client_errc::incompatible_field_type;
    }
};

// INTERVAL
template <>
struct field_is_compatible<types::pg_interval>
{
    static inline boost::system::error_code call(const protocol::field_description& desc)
    {
        return desc.type_oid == 1186 ? boost::system::error_code() : client_errc::incompatible_field_type;
    }
};

template <class T>
struct field_parse;

// DATE
template <>
struct field_parse<std::chrono::sys_days>
{
    static boost::system::error_code call(
        std::optional<std::span<const unsigned char>> from,
        const protocol::field_description& desc,
        std::chrono::sys_days& to
    );
};

// TIME
template <>
struct field_parse<std::chrono::microseconds>
{
    static boost::system::error_code call(
        std::optional<std::span<const unsigned char>> from,
        const protocol::field_description& desc,
        std::chrono::microseconds& to
    );
};

// TIMETZ
template <>
struct field_parse<types::pg_timetz>
{
    static boost::system::error_code call(
        std::optional<std::span<const unsigned char>> from,
        const protocol::field_description& desc,
        types::pg_timetz& to
    );
};

// TIMESTAMP
template <>
struct field_parse<types::pg_timestamp>
{
    static boost::system::error_code call(
        std::optional<std::span<const unsigned char>> from,
        const protocol::field_description& desc,
        types::pg_timestamp& to
    );
};

// TIMESTAMPTZ
template <>
struct field_parse<types::pg_timestamptz>
{
    static boost::system::error_code call(
        std::optional<std::span<const unsigned char>> from,
        const protocol::field_description& desc,
        types::pg_timestamptz& to
    );
};

// INTERVAL
template <>
struct field_parse<types::pg_interval>
{
    static boost::system::error_code call(
        std::optional<std::span<const unsigned char>> from,
        const protocol::field_description& desc,
        types::pg_interval& to
    );
};

}
#endif  // NATIVEPG_FIELD_TRAITS_DATETIME_HPP
