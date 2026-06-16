
#ifndef NATIVEPG_FIELD_TRAITS_JSON_HPP
#define NATIVEPG_FIELD_TRAITS_JSON_HPP

#include <chrono>

#include <boost/system/error_code.hpp>

#include "nativepg/types.hpp"

namespace nativepg::detail {

template <class T>
struct field_is_compatible;

// JSON
template <>
struct field_is_compatible<types::pg_json>
{
    static inline boost::system::error_code call(const protocol::field_description& desc)
    {
        return desc.type_oid == 114 ? boost::system::error_code() : client_errc::incompatible_field_type;
    }
};

// JSONB
template <>
struct field_is_compatible<types::pg_jsonb>
{
    static inline boost::system::error_code call(const protocol::field_description& desc)
    {
        return desc.type_oid == 3802 ? boost::system::error_code() : client_errc::incompatible_field_type;
    }
};

template <class T>
struct field_parse;

// JSON
template <>
struct field_parse<types::pg_json>
{
    static boost::system::error_code call(
        std::optional<std::span<const unsigned char>> from,
        const protocol::field_description& desc,
        types::pg_json& to
    );
};

// JSONB
template <>
struct field_parse<types::pg_jsonb>
{
    static boost::system::error_code call(
        std::optional<std::span<const unsigned char>> from,
        const protocol::field_description& desc,
        types::pg_jsonb& to
    );
};

}

#endif  // NATIVEPG_FIELD_TRAITS_JSON_HPP
