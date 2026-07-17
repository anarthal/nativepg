# nativepg: a native Postgres protocol library for C++

This library aims to be the equivalent of Boost.MySQL for the PostgreSQL database.

Scope:

- Serialization functions to serialize and parse protocol messages.
- Sans-IO network algorithms, in the form of state machine classes, to implement the
  distinct protocol functionalities.
- Serialization functions to serialize and parse the distinct data types
  that can appear in a row.
- A connection class that uses the the points above to expose a simple Asio/Corosio interface for postgres
  (akin to `mysql::any_connection`).
- Functions to securely generate queries dynamically (akin to `mysql::format_sql`).
- A connection pool class (akin to `mysql::connection_pool`).

This is still a work in progress.

Any feedback is welcome. Please feel free to open an issue with your comments.

# Type mapping (Work in progress)

## Static types (Of which the OIDs are known at compile time)
| PostgreSQL type               | Base OID | Array OID | C++ type                                                                                                         | Status Base | Status Array |
|-------------------------------|----------|-----------|------------------------------------------------------------------------------------------------------------------|-------------|--------------|
| `bool`                        | 16       | 1000      | `bool`                                                                                                           | DRAFT PR    | [TODO]       |
| `bytea`                       | 17       | 1001      | `std::vector<std::byte>`                                                                                         | DRAFT PR    | [TODO]       |
| `char`                        | 18       | 1002      | `char`                                                                                                           | TODO        | [TODO]       |
| `name`                        | 19       | 1003      | `?`                                                                                                              | TODO        | [TODO]       |
| `int2` / `smallint`           | 21       | 1005      | `std::int16_t`                                                                                                   | IMPLEMENTED | [TODO]       |
| `int4` / `integer`            | 23       | 1007      | `std::int32_t`                                                                                                   | IMPLEMENTED | [TODO]       |
| `int8` / `bigint`             | 20       | 1016      | `std::int64_t`                                                                                                   | IMPLEMENTED | [TODO]       |
| `float4` / `real`             | 700      | 1021      | `float`                                                                                                          | DRAFT PR    | [TODO]       |
| `float8` / `double precision` | 701      | 1022      | `double`                                                                                                         | DRAFT PR    | [TODO]       |
| `numeric` / `decimal`         | 1700     | 1231      | `boost::multiprecision::cpp_bin_float_50` / `boost::multiprecision::cpp_bin_float_100`                           | DRAFT PR    | [TODO]       |
| `text`                        | 25       | 1009      | `std::string` / `std::string_view`                                                                               | DRAFT PR    | [TODO]       |
| `varchar`                     | 1043     | 1015      | `std::string` / `std::string_view`                                                                               | DRAFT PR    | [TODO]       |
| `oid`                         | 26       | 1028      | `std::uint32_t`                                                                                                  | TODO        | [TODO]       |
| `json`                        | 114      | 199       | `boost::json::value`                                                                                             | DRAFT PR    | [TODO]       |
| `jsonb`                       | 3802     | 3807      | `boost::json::value`                                                                                             | DRAFT PR    | [TODO]       |
| `date`                        | 1082     | 1182      | `std::chrono::sys_days`                                                                                          | IMPLEMENTED | [TODO]       |
| `time`                        | 1083     | 1183      | `std::chrono::microseconds`                                                                                      | IMPLEMENTED | [TODO]       |
| `timetz`                      | 1266     | 1270      | `nativepg::types::pg_timetz { std::chrono::microseconds time_since_midnight; std::chrono::seconds utc_offset;};` | IMPLEMENTED | [TODO]       |
| `timestamp`                   | 1114     | 1184      | `std::chrono::local_time<std::chrono::microseconds>`                                                             | IMPLEMENTED | [TODO]       |
| `timestamptz`                 | 1184     | 1185      | `std::chrono::sys_time<std::chrono::microseconds>;`                                                              | IMPLEMENTED | [TODO]       |
| `interval`                    | 1186     | 1187      | `nativepg::types::pg_interval { int months; int days; std::chrono::microseconds time;};`                         | IMPLEMENTED | [TODO]       |

## Dynamic types
PostgreSQL supports dynamic types (aka user-defined types). Of which the OIDs are not known at compile time.
This library uses the `pg_type` table to resolve the OIDs at runtime.

### PostGIS
To integrate PostGIS cleanly into nativepg, this query must run during the driver's initialization/handshake phase (right after establishing a connection). 
This query resolves the accurate, runtime-assigned dynamic OIDs instantly:
```postgresql
SELECT
  typname,
  oid AS base_oid,
  typarray AS array_oid
FROM pg_type
WHERE typname IN ('geometry', 'geography', 'raster', 'box2d', 'box3d');
```
On my PostgreSQL 18 server with PostGIS 3.6.4 it returns:

| typname   | base\_oid | array\_oid | status base | status array |
|-----------|-----------|------------|-------------|--------------|
| geometry  | 24588     | 24596      | DRAFT PR    | DRAFT PR     |
| box3d     | 24615     | 24618      | DRAFT PR    | DRAFT PR     |
| box2d     | 24619     | 24622      | DRAFT PR    | DRAFT PR     |
| geography | 25288     | 25294      | DRAFT PR    | DRAFT PR     |
| raster    | 25665     | 25668      | DRAFT PR    | DRAFT PR     |

NOTE: Working on this in PR #42.