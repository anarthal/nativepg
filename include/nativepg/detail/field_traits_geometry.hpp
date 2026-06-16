
#ifndef NATIVEPG_FIELD_TRAITS_GEOMETRY_HPP
#define NATIVEPG_FIELD_TRAITS_GEOMETRY_HPP

#include <boost/geometry.hpp>
#include <boost/system/error_code.hpp>

#include "nativepg/protocol/describe.hpp"
#include "nativepg/types.hpp"

namespace nativepg::detail {

template <class T>
struct field_is_compatible;

// GEOMETRY
template <>
struct field_is_compatible<types::pg_geometry>
{
    static inline boost::system::error_code call(const protocol::field_description& desc)
    {
        return desc.type_oid == 16384 ? boost::system::error_code() : client_errc::incompatible_field_type;
    }
};

// GEOGRAPHY
template <>
struct field_is_compatible<types::pg_geography>
{
    static inline boost::system::error_code call(const protocol::field_description& desc)
    {
        return desc.type_oid == 16390 ? boost::system::error_code() : client_errc::incompatible_field_type;
    }
};

// BOX2D
template <>
struct field_is_compatible<types::pg_box2d>
{
    static inline boost::system::error_code call(const protocol::field_description& desc)
    {
        return desc.type_oid == 16395 ? boost::system::error_code() : client_errc::incompatible_field_type;
    }
};

// BOX3D
template <>
struct field_is_compatible<types::pg_box3d>
{
    static inline boost::system::error_code call(const protocol::field_description& desc)
    {
        return desc.type_oid == 16398 ? boost::system::error_code() : client_errc::incompatible_field_type;
    }
};


template <class T>
struct field_parse;


// GEOMETRY
template <>
struct field_parse<types::pg_geometry>
{
    static boost::system::error_code call(
        std::optional<std::span<const unsigned char>> from,
        const protocol::field_description& desc,
        types::pg_geometry& to
    );
};

// GEOGRAPHY
template <>
struct field_parse<types::pg_geography>
{
    static boost::system::error_code call(
        std::optional<std::span<const unsigned char>> from,
        const protocol::field_description& desc,
        types::pg_geography& to
    );
};

// BOX2D
template <>
struct field_parse<types::pg_box2d>
{
    static boost::system::error_code call(
        std::optional<std::span<const unsigned char>> from,
        const protocol::field_description& desc,
        types::pg_box2d& to
    );
};

// BOX3D
template <>
struct field_parse<types::pg_box3d>
{
    static boost::system::error_code call(
        std::optional<std::span<const unsigned char>> from,
        const protocol::field_description& desc,
        types::pg_box3d& to
    );
};

}
#endif  // NATIVEPG_FIELD_TRAITS_GEOMETRY_HPP
