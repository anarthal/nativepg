#ifndef NATIVEPG_FIELD_TRAITS_JSON_HPP
#define NATIVEPG_FIELD_TRAITS_JSON_HPP

#ifdef NATIVEPG_JSON

#include <boost/system/error_code.hpp>

#include "nativepg/client_errc.hpp"
#include "nativepg/field_view.hpp"
#include "nativepg/protocol/describe.hpp"
#include "nativepg/types/json.hpp"

namespace nativepg::detail {

template <class T>
struct field_is_compatible;

// JSON & JSONB
template <>
struct field_is_compatible<boost::json::value>
{
    static inline boost::system::error_code call(const protocol::field_description& desc)
    {
        return (desc.type_oid == 114 || desc.type_oid == 3802) ? boost::system::error_code() : client_errc::incompatible_field_type;
    }
};


template <class T>
struct field_parse;

// JSON => boost::json::value
template <>
struct field_parse<boost::json::value>
{
    static inline boost::system::error_code call(
        field_view from,
        const protocol::field_description& desc,
        boost::json::value& to
    )
    {
        if (from.is_null())
            return client_errc::unexpected_null;

        if (desc.type_oid == 3802 /*jsonb*/)
        {
            return desc.fmt_code == protocol::format_code::text
                ? types::parse_text_jsonb(from.data(), to)
                : types::parse_binary_jsonb(from.data(), to);
        }
        else
        {
            return desc.fmt_code == protocol::format_code::text
                ? types::parse_text_json(from.data(), to)
                : types::parse_binary_json(from.data(), to);
        }
    }
};

}  // namespace nativepg::detail

#endif  // NATIVEPG_JSON
#endif  // NATIVEPG_FIELD_TRAITS_JSON_HPP