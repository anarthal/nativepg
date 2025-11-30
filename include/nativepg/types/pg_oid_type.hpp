//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_TYPES_PG_OID_TYPE_HPP
#define NATIVEPG_TYPES_PG_OID_TYPE_HPP

namespace nativepg {
namespace types {

/// PostgreSQL built-in type OIDs.
/// Source: pg_type.h (PostgreSQL source)
enum class pg_oid_type : std::uint32_t
{
    // Boolean & binary
    bool_           = 16,
    bytea           = 17,
    char_           = 18,
    name            = 19,
    int8            = 20,
    int2            = 21,
    int2vector      = 22,
    int4            = 23,
    regproc         = 24,
    text            = 25,
    oid             = 26,
    tid             = 27,
    xid             = 28,
    cid             = 29,
    oidvector       = 30,

    // Numeric types
    float4          = 700,
    float8          = 701,
    money           = 790,
    numeric         = 1700,

    // Character string types
    varchar         = 1043,
    bpchar          = 1042,
    json            = 114,
    jsonb           = 3802,
    uuid            = 2950,
    xml             = 142,

    // Date & time types
    date            = 1082,
    time            = 1083,
    timestamp       = 1114,
    timestamptz     = 1184,
    interval        = 1186,

    // Network address types
    cidr            = 650,
    inet            = 869,
    macaddr         = 829,
    macaddr8        = 774,

    // Geometric types
    pointtype       = 600,
    lsegtype        = 601,
    path            = 602,
    box             = 603,
    polygon         = 604,
    line            = 628,
    circle          = 718,

    // Arrays (base OID + 1000)
    boolarray       = 1000,
    int2array       = 1005,
    int4array       = 1007,
    textarray       = 1009,
    oidarray        = 1028,
    float4array     = 1021,
    float8array     = 1022,
    varchararray    = 1015,
    bpchararray     = 1014,
    numericarray    = 1231,
    uuidarray       = 2951,
    jsonarray       = 199,
    jsonbarray      = 3807,
    timestamptzarray= 1185,

    // Object identifiers for composite/record
    record          = 2249,
    recordarray     = 2287,
    void_           = 2278,
    unknown         = 705,
    any             = 2276,
    anyarray        = 2277,

    // Range / domain
    int4range       = 3904,
    int8range       = 3926,
    tsrange         = 3908,
    tstzrange       = 3910,
    daterange       = 3912,

    // PostGIS (commonly used extension)
    geometry        = 3004,   // GEOMETRY
    geography       = 4326,   // OFTEN SAME AS epsg:4326 (VARIES BY INSTALL)

    // Special
    regclass        = 2205,
    regtype         = 2206,
    regnamespace    = 4089,
    regrole         = 4096,
    regconfig       = 3734,
    regdictionary   = 3769
};

/// Optional helper: stringify a known OID
inline const char* to_string(pg_oid_type oid)
{
    switch (oid)
    {
    case pg_oid_type::bool_: return "bool";
    case pg_oid_type::bytea: return "bytea";
    case pg_oid_type::int2: return "int2";
    case pg_oid_type::int4: return "int4";
    case pg_oid_type::int8: return "int8";
    case pg_oid_type::float4: return "float4";
    case pg_oid_type::float8: return "float8";
    case pg_oid_type::numeric: return "numeric";
    case pg_oid_type::text: return "text";
    case pg_oid_type::varchar: return "varchar";
    case pg_oid_type::json: return "json";
    case pg_oid_type::jsonb: return "jsonb";
    case pg_oid_type::uuid: return "uuid";
    case pg_oid_type::date: return "date";
    case pg_oid_type::time: return "time";
    case pg_oid_type::timestamp: return "timestamp";
    case pg_oid_type::timestamptz: return "timestamptz";
    case pg_oid_type::interval: return "interval";
    case pg_oid_type::pointtype: return "point";
    case pg_oid_type::polygon: return "polygon";
    case pg_oid_type::geometry: return "geometry";
    case pg_oid_type::geography: return "geography";
    default: return "unknown";
    }
}

}
}

#endif
