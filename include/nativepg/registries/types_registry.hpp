//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_TYPES_REGISTRY_HPP
#define NATIVEPG_TYPES_REGISTRY_HPP

#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <cstdint>
#include <ranges>
#include <algorithm>

#include <boost/describe/class.hpp>

namespace nativepg {


struct type_info
{
    std::uint32_t type_oid;
    std::string schema_name;
    std::string type_name;
    char type_delimiter;
    std::optional<std::string> extension_name;
    std::optional<short> schema_path_seqno;
    std::optional<std::uint32_t> array_element_oid;
    char type;
    short storage_bytes;
};
BOOST_DESCRIBE_STRUCT( type_info, (), (type_oid, schema_name, type_name, type_delimiter, extension_name, schema_path_seqno, array_element_oid, type, storage_bytes));


class types_registry
{
    inline static const std::string_view main_sql = R"sql(
WITH path_order AS (
    -- Unnest the search path and assign sequence numbers
    SELECT
        schema_name,
        ordinality::int2 AS seqno
    FROM unnest(current_schemas(false)) WITH ORDINALITY AS t(schema_name, ordinality)
),
type_extensions AS (
    -- Resolve which types belong to which extensions via pg_depend
    SELECT
        objid AS type_oid,
        ext.extname AS extension_name
    FROM pg_catalog.pg_depend dep
    JOIN pg_catalog.pg_extension ext ON dep.refobjid = ext.oid
    WHERE dep.classid = 'pg_catalog.pg_type'::regclass
      AND dep.refclassid = 'pg_catalog.pg_extension'::regclass
      AND dep.deptype = 'e' -- 'e' means the object is an internal part of the extension
)
SELECT
    -- oid is unique in database
    t.oid AS type_oid,
    -- type name is unique in schema
    n.nspname AS schema_name,
    t.typname AS type_name,
    t.typdelim AS type_delimiter,
    ext.extension_name, -- NULL if it is a native built-in type or independent UDT
    -- Only return the search_path sequence number if the type is explicitly visible in the search_path
    CASE
        WHEN pg_catalog.pg_type_is_visible(t.oid) THEN p.seqno
        ELSE NULL
    END AS schema_path_seqno,
    -- Is it an array? True if the type has an element type and typlen is variable (-1)
    CASE
        WHEN t.typlen = -1 and t.typelem !=0 THEN t.typelem
        ELSE NULL
    END AS array_element_oid,
    -- Is it an Enum type? ('e' stands for enum in typtype)
    -- Is it a Domain type? ('d' stands for domain in typtype)
    -- Is it a Composite type? ('c' stands for composite in typtype)
    -- Is it a Pseudo-type? ('p' stands for pseudo-type in typtype, like record, void)
    t.typtype as type,
    -- Storage size in bytes. Returns -1 if the size is variable-length (like text)
    t.typlen AS storage_bytes
FROM pg_catalog.pg_type t
JOIN pg_catalog.pg_namespace n ON n.oid = t.typnamespace
-- Left join ensures we see ALL types, even if they aren't in the active search_path
LEFT JOIN path_order p ON p.schema_name = n.nspname
LEFT JOIN type_extensions ext ON ext.type_oid = t.oid
WHERE
    -- 0. We don't want the static oids
    t.oid >= 16400
AND
    -- 1. Exclude composite types directly tied to real tables, views, or foreign tables
    (t.typrelid = 0 OR EXISTS (
        SELECT 1
        FROM pg_catalog.pg_class c
        WHERE c.oid = t.typrelid AND c.relkind = 'c' -- Keep only standalone composite types
    ))
    -- 2. Exclude the automatic array types generated for those table composite types
    AND NOT EXISTS (
        SELECT 1
        FROM pg_catalog.pg_type base
        JOIN pg_catalog.pg_class c ON c.oid = base.typrelid
        WHERE base.oid = t.typelem
          AND c.relkind != 'c'
    )
ORDER BY
    type_name,
    schema_path_seqno nulls last -- < Get first type in schema_path
;
    )sql";

    std::unique_ptr<std::vector<type_info>> table_;

public:
    // Static members
    inline static const std::string_view load_sql() { return main_sql; }


    // Constructors and assignment operators
    types_registry(const std::vector<type_info>& table) : table_(std::make_unique<std::vector<type_info>>(table)) {}
    types_registry(const types_registry&) = delete;
    types_registry(types_registry&&) = delete;
    types_registry& operator=(const types_registry&) = delete;
    types_registry& operator=(types_registry&&) = delete;
    ~types_registry() = default;


    // Range support, so instances can be iterated
    auto begin() const { return table_->begin(); }
    auto end() const { return table_->end(); }
    std::size_t size() const { return table_->size(); }
    bool empty() const { return table_->empty(); }


    const type_info& find_by_oid(std::uint32_t oid) const
    {
        auto it = std::ranges::find_if(*table_, [oid](const type_info& info) { return info.type_oid == oid; });
        return *it;
    }

    const type_info& find_by_type_name(const std::string& schema_name, const std::string& type_name) const
    {
        auto it = std::ranges::find_if(*table_, [&schema_name, &type_name](const type_info& info) {
            return info.schema_name == schema_name && info.type_name == type_name;
        });
        return *it;
    }

    const type_info& find_by_type_name(const std::string& type_name) const
    {
        auto it = std::ranges::find_if(*table_, [&type_name](const type_info& info) {
            return info.type_name == type_name;
        });
        return *it;
    }
};

}  // namespace nativepg

#endif // NATIVEPG_TYPES_REGISTRY_HPP
