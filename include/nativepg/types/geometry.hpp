#ifndef NATIVEPG_TYPES_GEOMETRY_HPP
#define NATIVEPG_TYPES_GEOMETRY_HPP

#include <boost/system/error_code.hpp>
#include <boost/geometry.hpp>

#include <variant>
#include <span>
#include <string_view>

#include "nativepg/client_errc.hpp"

namespace nativepg::types {

using boost::system::error_code;


/*
| Type       | Category     | OID   | Storage size | Precision              | Minimum value        | Maximum value        |
|------------|--------------|-------|--------------|------------------------|----------------------|----------------------|
| geometry   | spatial      | 16384 | Up to 1GB    | double precision       | -system limit        | +system limit        |
| geography  | spatial      | 16390 | Up to 1GB    | double precision       | -180° Lon / -90° Lat | +180° Lon / +90° Lat |
| box2d      | bounding-box | 16395 | 32 bytes     | single precision float | -float max           | +float max           |
| box3d      | bounding-box | 16398 | 48 bytes     | single precision float | -float max           | +float max           |
*/

// Type mapping
template <typename TPrecision = float, std::size_t TDimension = 2, typename TCoordSystem = boost::geometry::cs::cartesian>
class pg_geometry
{
public:
    typedef boost::geometry::model::point<TPrecision, TDimension, TCoordSystem> point_t;
    typedef boost::geometry::model::linestring<point_t> line_t;
    typedef boost::geometry::model::polygon<point_t> polygon_t;

    typedef boost::geometry::model::multi_point<point_t> multi_point_t;
    typedef boost::geometry::model::multi_linestring<line_t> multi_line_t;
    typedef boost::geometry::model::multi_polygon<polygon_t> multi_polygon_t;

    typedef std::variant<point_t, line_t, polygon_t, multi_point_t, multi_line_t, multi_polygon_t> geometry_t;

    //operator geometry_t&() { return _storage; }

private:
    geometry_t _storage;

    friend std::ostream& operator<<(std::ostream& os, const pg_geometry& g)
    {
        std::visit([&os](const auto& geom) {
            os << boost::geometry::wkt(geom);
        }, g._storage);
        return os;
    }
};

class pg_geography
{

};

class pg_box2d
{

};

class pg_box3d
{

};


namespace detail {


}  // namespace detail



// GEOMETRY => pg_geometry (TEXT)
template <class T = types::pg_geometry<>>
constexpr error_code parse_text_geometry(std::span<const unsigned char> from, T& to) noexcept
{
    error_code ec{};

    std::string_view text{reinterpret_cast<const char*>(from.data()), from.size()};

    return ec;
}

// GEOMETRY => pg_geometry (BINARY)
template <class T = types::pg_geometry<>>
constexpr error_code parse_binary_geometry(std::span<const unsigned char> from, T& to) noexcept
{
    error_code ec{};

    std::string_view bytes{reinterpret_cast<const char*>(from.data()), from.size()};

    return ec;
}


// GEOGRAPHY => pg_geography (TEXT)
template <class T = types::pg_geography>
constexpr error_code parse_text_geography(std::span<const unsigned char> from, T& to) noexcept
{
    error_code ec{};

    std::string_view text{reinterpret_cast<const char*>(from.data()), from.size()};

    return ec;
}

// GEOGRAPHY => pg_geography (BINARY)
template <class T = types::pg_geography>
constexpr error_code parse_binary_geography(std::span<const unsigned char> from, T& to) noexcept
{
    error_code ec{};

    std::string_view bytes{reinterpret_cast<const char*>(from.data()), from.size()};

    return ec;
}


// BOX2D => pg_box2d (TEXT)
template <class T = types::pg_box2d>
constexpr error_code parse_text_box2d(std::span<const unsigned char> from, T& to) noexcept
{
    error_code ec{};

    std::string_view text{reinterpret_cast<const char*>(from.data()), from.size()};

    return ec;
}

// BOX2D => pg_box2d (BINARY)
template <class T = types::pg_box2d>
constexpr error_code parse_binary_box2d(std::span<const unsigned char> from, T& to) noexcept
{
    error_code ec{};

    std::string_view bytes{reinterpret_cast<const char*>(from.data()), from.size()};

    return ec;
}


// BOX3D => pg_box3d (TEXT)
template <class T = types::pg_box3d>
constexpr error_code parse_text_box3d(std::span<const unsigned char> from, T& to) noexcept
{
    error_code ec{};

    std::string_view text{reinterpret_cast<const char*>(from.data()), from.size()};

    return ec;
}

// BOX3D => pg_box3d (BINARY)
template <class T = types::pg_box3d>
constexpr error_code parse_binary_box3d(std::span<const unsigned char> from, T& to) noexcept
{
    error_code ec{};

    std::string_view bytes{reinterpret_cast<const char*>(from.data()), from.size()};

    return ec;
}

}

#endif  // NATIVEPG_TYPES_GEOMETRY_HPP
