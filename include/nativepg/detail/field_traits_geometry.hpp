
#ifndef NATIVEPG_FIELD_TRAITS_GEOMETRY_HPP
#define NATIVEPG_FIELD_TRAITS_GEOMETRY_HPP

#include <boost/geometry.hpp>
#include <boost/system/error_code.hpp>

#include "nativepg/protocol/describe.hpp"
#include "nativepg/types/geometry.hpp"

namespace nativepg::detail {

template <class T>
struct field_is_compatible;

// GEOMETRY
template <typename TPrecision, std::size_t TDimension, typename TCoordSystem>
struct field_is_compatible<types::pg_geometry<TPrecision, TDimension, TCoordSystem>>
{
    static inline boost::system::error_code call(const protocol::field_description& desc)
    {
        return desc.type_oid == 24588 ? boost::system::error_code() : client_errc::incompatible_field_type;
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
template <typename TPrecision, std::size_t TDimension, typename TCoordSystem>
struct field_parse<types::pg_geometry<TPrecision, TDimension, TCoordSystem>>
{
    static inline boost::system::error_code call(
         std::optional<std::span<const unsigned char>> from,
         const protocol::field_description& desc,
         types::pg_geometry<TPrecision, TDimension, TCoordSystem>& to
     )
    {
        if (!from.has_value())
            return client_errc::unexpected_null;
        return desc.fmt_code == protocol::format_code::text
            ? types::parse_text_geometry(*from, to)
            : types::parse_binary_geometry(*from, to);
    }
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




// GEOGRAPHY => pg_geography
inline boost::system::error_code field_parse<types::pg_geography>::call(
    std::optional<std::span<const unsigned char>> from,
    const protocol::field_description& desc,
    types::pg_geography& to
)
{
    if (!from.has_value())
        return client_errc::unexpected_null;
    BOOST_ASSERT(desc.type_oid == 16390);
    return desc.fmt_code == protocol::format_code::text ?
        parse_text_geography(*from, to) :
        parse_binary_geography(*from, to);
}

// BOX2D => pg_box2d
inline boost::system::error_code field_parse<types::pg_box2d>::call(
    std::optional<std::span<const unsigned char>> from,
    const protocol::field_description& desc,
    types::pg_box2d& to
)
{
    if (!from.has_value())
        return client_errc::unexpected_null;
    BOOST_ASSERT(desc.type_oid == 16395);
    return desc.fmt_code == protocol::format_code::text ?
        parse_text_box2d(*from, to) :
        parse_binary_box2d(*from, to);
}

// BOX3D => pg_box3d
inline boost::system::error_code field_parse<types::pg_box3d>::call(
    std::optional<std::span<const unsigned char>> from,
    const protocol::field_description& desc,
    types::pg_box3d& to
)
{
    if (!from.has_value())
        return client_errc::unexpected_null;
    BOOST_ASSERT(desc.type_oid == 16398);
    return desc.fmt_code == protocol::format_code::text ?
        parse_text_box3d(*from, to) :
        parse_binary_box3d(*from, to);
}

}
#endif  // NATIVEPG_FIELD_TRAITS_GEOMETRY_HPP
