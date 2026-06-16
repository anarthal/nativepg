#ifndef NATIVEPG_TYPES_GEOMETRY_HPP
#define NATIVEPG_TYPES_GEOMETRY_HPP

#include <boost/system/error_code.hpp>
#include <boost/geometry.hpp>

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
class pg_geometry
{

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
template <class T = types::pg_geometry>
constexpr error_code parse_text_geometry(std::span<const unsigned char> from, T& to) noexcept
{
    error_code ec{};

    std::string_view text{reinterpret_cast<const char*>(from.data()), from.size()};

    return ec;
}

// GEOMETRY => pg_geometry (BINARY)
template <class T = types::pg_geometry>
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
