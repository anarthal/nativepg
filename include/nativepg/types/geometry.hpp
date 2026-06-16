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

    void set(geometry_t g) { _storage = std::move(g); }

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

inline int hex_val(unsigned char c) noexcept
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Decode a hex string into bytes. Returns false on invalid input.
inline bool hex_decode(std::string_view hex, std::vector<unsigned char>& out) noexcept
{
    if (hex.size() % 2 != 0) return false;
    out.resize(hex.size() / 2);
    for (std::size_t i = 0; i < out.size(); ++i)
    {
        int hi = hex_val(static_cast<unsigned char>(hex[2 * i]));
        int lo = hex_val(static_cast<unsigned char>(hex[2 * i + 1]));
        if (hi < 0 || lo < 0) return false;
        out[i] = static_cast<unsigned char>((hi << 4) | lo);
    }
    return true;
}

// Cursor for reading EWKB bytes with byte-order awareness
struct ewkb_reader
{
    const unsigned char* p;
    const unsigned char* end;
    bool little_endian{true};

    bool has(std::size_t n) const noexcept { return (end - p) >= static_cast<std::ptrdiff_t>(n); }

    bool read_byte(uint8_t& v) noexcept
    {
        if (!has(1)) return false;
        v = *p++;
        return true;
    }

    bool read_uint32(uint32_t& v) noexcept
    {
        if (!has(4)) return false;
        if (little_endian)
            v = boost::endian::endian_load<uint32_t, 4, boost::endian::order::little>(p);
        else
            v = boost::endian::endian_load<uint32_t, 4, boost::endian::order::big>(p);
        p += 4;
        return true;
    }

    bool read_double(double& v) noexcept
    {
        if (!has(8)) return false;
        uint64_t raw;
        if (little_endian)
            raw = boost::endian::endian_load<uint64_t, 8, boost::endian::order::little>(p);
        else
            raw = boost::endian::endian_load<uint64_t, 8, boost::endian::order::big>(p);
        p += 8;
        std::memcpy(&v, &raw, 8);
        return true;
    }
};

// EWKB type constants
constexpr uint32_t EWKB_TYPE_MASK   = 0x1FFFFFFF;
constexpr uint32_t EWKB_FLAG_Z      = 0x80000000;
constexpr uint32_t EWKB_FLAG_M      = 0x40000000;
constexpr uint32_t EWKB_FLAG_SRID   = 0x20000000;

constexpr uint32_t EWKB_POINT             = 1;
constexpr uint32_t EWKB_LINESTRING        = 2;
constexpr uint32_t EWKB_POLYGON          = 3;
constexpr uint32_t EWKB_MULTIPOINT        = 4;
constexpr uint32_t EWKB_MULTILINESTRING   = 5;
constexpr uint32_t EWKB_MULTIPOLYGON      = 6;

// Forward declarations
template <class PointT>
bool read_ewkb_point(ewkb_reader& r, uint32_t flags, PointT& pt) noexcept;

template <class PointT>
bool read_ewkb_linestring(ewkb_reader& r, boost::geometry::model::linestring<PointT>& ls) noexcept;

template <class PointT>
bool read_ewkb_ring(ewkb_reader& r, boost::geometry::model::ring<PointT>& ring) noexcept;

template <class PointT>
bool read_ewkb_polygon(ewkb_reader& r, boost::geometry::model::polygon<PointT>& poly) noexcept;

// --- Read a single point's coordinates (no sub-header) ---
template <class PointT>
bool read_ewkb_point(ewkb_reader& r, uint32_t flags, PointT& pt) noexcept
{
    double x{}, y{};
    if (!r.read_double(x) || !r.read_double(y)) return false;
    // Skip Z and/or M if present
    if (flags & EWKB_FLAG_Z) { double z{}; if (!r.read_double(z)) return false; }
    if (flags & EWKB_FLAG_M) { double m{}; if (!r.read_double(m)) return false; }
    boost::geometry::set<0>(pt, static_cast<typename boost::geometry::coordinate_type<PointT>::type>(x));
    boost::geometry::set<1>(pt, static_cast<typename boost::geometry::coordinate_type<PointT>::type>(y));
    return true;
}

// --- Read a ring (used by polygon) ---
template <class PointT>
bool read_ewkb_ring(ewkb_reader& r, boost::geometry::model::ring<PointT>& ring) noexcept
{
    uint32_t count{};
    if (!r.read_uint32(count)) return false;
    ring.resize(count);
    for (auto& pt : ring)
        if (!read_ewkb_point(r, 0, pt)) return false;  // rings have no Z/M flags of their own
    return true;
}

template <class PointT>
bool read_ewkb_linestring(ewkb_reader& r, boost::geometry::model::linestring<PointT>& ls) noexcept
{
    uint32_t count{};
    if (!r.read_uint32(count)) return false;
    ls.resize(count);
    for (auto& pt : ls)
        if (!read_ewkb_point(r, 0, pt)) return false;
    return true;
}

template <class PointT>
bool read_ewkb_polygon(ewkb_reader& r, boost::geometry::model::polygon<PointT>& poly) noexcept
{
    uint32_t ring_count{};
    if (!r.read_uint32(ring_count)) return false;
    if (ring_count == 0) return true;

    // First ring = exterior
    if (!read_ewkb_ring(r, poly.outer())) return false;
    // Remaining rings = holes
    poly.inners().resize(ring_count - 1);
    for (auto& hole : poly.inners())
        if (!read_ewkb_ring(r, hole)) return false;
    return true;
}

// Read one complete EWKB geometry (with its own byte-order + type header)
template <class GeomVariant>
bool read_ewkb_geometry(ewkb_reader& r, GeomVariant& out) noexcept;

template <class GeomVariant>
bool read_ewkb_geometry(ewkb_reader& r, GeomVariant& out) noexcept
{
    // Byte order
    uint8_t bo{};
    if (!r.read_byte(bo)) return false;
    r.little_endian = (bo == 1);

    // Type word
    uint32_t type_word{};
    if (!r.read_uint32(type_word)) return false;
    uint32_t base_type = type_word & EWKB_TYPE_MASK;
    uint32_t flags     = type_word & ~EWKB_TYPE_MASK;

    // Skip SRID if present
    if (flags & EWKB_FLAG_SRID)
    {
        uint32_t srid{};
        if (!r.read_uint32(srid)) return false;
    }

    using point_t        = typename std::variant_alternative_t<0, GeomVariant>;
    using line_t         = typename std::variant_alternative_t<1, GeomVariant>;
    using polygon_t      = typename std::variant_alternative_t<2, GeomVariant>;
    using multi_point_t  = typename std::variant_alternative_t<3, GeomVariant>;
    using multi_line_t   = typename std::variant_alternative_t<4, GeomVariant>;
    using multi_polygon_t= typename std::variant_alternative_t<5, GeomVariant>;

    switch (base_type)
    {
        case EWKB_POINT:
        {
            point_t pt;
            if (!read_ewkb_point(r, flags, pt)) return false;
            out = std::move(pt);
            return true;
        }
        case EWKB_LINESTRING:
        {
            line_t ls;
            if (!read_ewkb_linestring(r, ls)) return false;
            out = std::move(ls);
            return true;
        }
        case EWKB_POLYGON:
        {
            polygon_t poly;
            if (!read_ewkb_polygon(r, poly)) return false;
            out = std::move(poly);
            return true;
        }
        case EWKB_MULTIPOINT:
        {
            multi_point_t mp;
            uint32_t count{};
            if (!r.read_uint32(count)) return false;
            mp.resize(count);
            for (auto& pt : mp)
            {
                // Each sub-geometry has its own header
                GeomVariant sub;
                if (!read_ewkb_geometry(r, sub)) return false;
                pt = std::get<point_t>(sub);
            }
            out = std::move(mp);
            return true;
        }
        case EWKB_MULTILINESTRING:
        {
            multi_line_t ml;
            uint32_t count{};
            if (!r.read_uint32(count)) return false;
            ml.resize(count);
            for (auto& ls : ml)
            {
                GeomVariant sub;
                if (!read_ewkb_geometry(r, sub)) return false;
                ls = std::get<line_t>(sub);
            }
            out = std::move(ml);
            return true;
        }
        case EWKB_MULTIPOLYGON:
        {
            multi_polygon_t mpoly;
            uint32_t count{};
            if (!r.read_uint32(count)) return false;
            mpoly.resize(count);
            for (auto& poly : mpoly)
            {
                GeomVariant sub;
                if (!read_ewkb_geometry(r, sub)) return false;
                poly = std::get<polygon_t>(sub);
            }
            out = std::move(mpoly);
            return true;
        }
        default: return false;
    }
}

}  // namespace detail



// GEOMETRY => pg_geometry (TEXT)
// PostGIS always sends geometry as a hex-encoded EWKB string, even in text mode.
template <class T = types::pg_geometry<>>
inline error_code parse_text_geometry(std::span<const unsigned char> from, T& to) noexcept
{
    std::string_view hex{reinterpret_cast<const char*>(from.data()), from.size()};

    std::vector<unsigned char> bytes;
    if (!detail::hex_decode(hex, bytes))
        return client_errc::protocol_value_error;

    detail::ewkb_reader r{bytes.data(), bytes.data() + bytes.size()};
    typename T::geometry_t variant;
    if (!detail::read_ewkb_geometry(r, variant))
        return client_errc::protocol_value_error;

    to.set(std::move(variant));
    return error_code{};
}

// GEOMETRY => pg_geometry (BINARY)
// Binary mode sends raw EWKB bytes (no hex encoding).
template <class T = types::pg_geometry<>>
inline error_code parse_binary_geometry(std::span<const unsigned char> from, T& to) noexcept
{
    detail::ewkb_reader r{from.data(), from.data() + from.size()};
    typename T::geometry_t variant;
    if (!detail::read_ewkb_geometry(r, variant))
        return client_errc::protocol_value_error;

    to.set(std::move(variant));
    return error_code{};
}


// GEOGRAPHY => pg_geography (TEXT)
template <class T = types::pg_geography>
constexpr error_code parse_text_geography(std::span<const unsigned char> from, [[maybe_unused]] T& to) noexcept
{
    error_code ec{};

    std::string_view text{reinterpret_cast<const char*>(from.data()), from.size()};

    return ec;
}

// GEOGRAPHY => pg_geography (BINARY)
template <class T = types::pg_geography>
constexpr error_code parse_binary_geography(std::span<const unsigned char> from, [[maybe_unused]] T& to) noexcept
{
    error_code ec{};

    std::string_view bytes{reinterpret_cast<const char*>(from.data()), from.size()};

    return ec;
}


// BOX2D => pg_box2d (TEXT)
template <class T = types::pg_box2d>
constexpr error_code parse_text_box2d(std::span<const unsigned char> from, [[maybe_unused]] T& to) noexcept
{
    error_code ec{};

    std::string_view text{reinterpret_cast<const char*>(from.data()), from.size()};

    return ec;
}

// BOX2D => pg_box2d (BINARY)
template <class T = types::pg_box2d>
constexpr error_code parse_binary_box2d(std::span<const unsigned char> from, [[maybe_unused]] T& to) noexcept
{
    error_code ec{};

    std::string_view bytes{reinterpret_cast<const char*>(from.data()), from.size()};

    return ec;
}


// BOX3D => pg_box3d (TEXT)
template <class T = types::pg_box3d>
constexpr error_code parse_text_box3d(std::span<const unsigned char> from, [[maybe_unused]] T& to) noexcept
{
    error_code ec{};

    std::string_view text{reinterpret_cast<const char*>(from.data()), from.size()};

    return ec;
}

// BOX3D => pg_box3d (BINARY)
template <class T = types::pg_box3d>
constexpr error_code parse_binary_box3d(std::span<const unsigned char> from, [[maybe_unused]] T& to) noexcept
{
    error_code ec{};

    std::string_view bytes{reinterpret_cast<const char*>(from.data()), from.size()};

    return ec;
}

}

#endif  // NATIVEPG_TYPES_GEOMETRY_HPP
