//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//


#ifndef NATIVEPG_SRC_TYPES_HPP
#define NATIVEPG_SRC_TYPES_HPP

#include <cstdint>

namespace nativepg {

/// PostgreSQL built-in type OIDs.
/// Source: pg_type.h (PostgreSQL source)
enum TypeOid : uint32_t
{
    // Boolean & binary
    BOOLOID            = 16,
    BYTEAOID           = 17,
    CHAROID            = 18,
    NAMEOID            = 19,
    INT8OID            = 20,
    INT2OID            = 21,
    INT2VECTOROID      = 22,
    INT4OID            = 23,
    REGPROCOID         = 24,
    TEXTOID            = 25,
    OIDOID             = 26,
    TIDOID             = 27,
    XIDOID             = 28,
    CIDOID             = 29,
    OIDVECTOROID       = 30,

    // Numeric types
    FLOAT4OID          = 700,
    FLOAT8OID          = 701,
    MONEYOID           = 790,
    NUMERICOID         = 1700,

    // Character string types
    VARCHAROID         = 1043,
    BPCHAROID          = 1042,
    JSONOID            = 114,
    JSONBOID           = 3802,
    UUIDOID            = 2950,
    XMLOID             = 142,

    // Date & time types
    DATEOID            = 1082,
    TIMEOID            = 1083,
    TIMESTAMPOID       = 1114,
    TIMESTAMPTZOID     = 1184,
    INTERVALOID        = 1186,

    // Network address types
    CIDROID            = 650,
    INETOID            = 869,
    MACADDROID         = 829,
    MACADDR8OID        = 774,

    // Geometric types
    POINTTYPEOID       = 600,
    LSEGTYPEOID        = 601,
    PATHOID            = 602,
    BOXOID             = 603,
    POLYGONOID         = 604,
    LINEOID            = 628,
    CIRCLEOID          = 718,

    // Arrays (base OID + 1000)
    BOOLARRAYOID       = 1000,
    INT2ARRAYOID       = 1005,
    INT4ARRAYOID       = 1007,
    TEXTARRAYOID       = 1009,
    OIDARRAYOID        = 1028,
    FLOAT4ARRAYOID     = 1021,
    FLOAT8ARRAYOID     = 1022,
    VARCHARARRAYOID    = 1015,
    BPCHARARRAYOID     = 1014,
    NUMERICARRAYOID    = 1231,
    UUIDARRAYOID       = 2951,
    JSONARRAYOID       = 199,
    JSONBARRAYOID      = 3807,
    TIMESTAMPTZARRAYOID= 1185,

    // Object identifiers for composite/record
    RECORDOID          = 2249,
    RECORDARRAYOID     = 2287,
    VOIDOID            = 2278,
    UNKNOWNOID         = 705,
    ANYOID             = 2276,
    ANYARRAYOID        = 2277,

    // Range / domain
    INT4RANGEOID       = 3904,
    INT8RANGEOID       = 3926,
    TSRANGEOID         = 3908,
    TSTZRANGEOID       = 3910,
    DATERANGEOID       = 3912,

    // PostGIS (commonly used extension)
    GEOMETRYOID        = 3004,   // geometry
    GEOGRAPHYOID       = 4326,   // often same as EPSG:4326 (varies by install)

    // Special
    REGCLASSOID        = 2205,
    REGTYPEOID         = 2206,
    REGNAMESPACEOID    = 4089,
    REGROLEOID         = 4096,
    REGCONFIGOID       = 3734,
    REGDICTIONARYOID   = 3769
};

/// Optional helper: stringify a known OID
inline const char* to_string(TypeOid oid)
{
    switch (oid)
    {
        case BOOLOID: return "bool";
        case BYTEAOID: return "bytea";
        case INT2OID: return "int2";
        case INT4OID: return "int4";
        case INT8OID: return "int8";
        case FLOAT4OID: return "float4";
        case FLOAT8OID: return "float8";
        case NUMERICOID: return "numeric";
        case TEXTOID: return "text";
        case VARCHAROID: return "varchar";
        case JSONOID: return "json";
        case JSONBOID: return "jsonb";
        case UUIDOID: return "uuid";
        case DATEOID: return "date";
        case TIMEOID: return "time";
        case TIMESTAMPOID: return "timestamp";
        case TIMESTAMPTZOID: return "timestamptz";
        case INTERVALOID: return "interval";
        case POINTTYPEOID: return "point";
        case POLYGONOID: return "polygon";
        case GEOMETRYOID: return "geometry";
        case GEOGRAPHYOID: return "geography";
        default: return "unknown";
    }
}

}  // namespace nativepg

#endif
