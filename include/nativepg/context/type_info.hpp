//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_CONTEXT_TYPE_INFO_HPP
#define NATIVEPG_CONTEXT_TYPE_INFO_HPP

#include <optional>
#include <string>
#include <vector>

#include "nativepg/context/context.hpp"
#include "nativepg/dynamic_resultset.hpp"

namespace nativepg::context {


struct type_info_tag {};
template <>
struct context_state_loader<type_info_tag> {
    struct state_type : public context_state {
        struct record
        {
            std::uint32_t type_oid;
            std::string schema_name;
            std::string type_name;
            std::optional<std::string> extension_name; // NULL if it is a native built-in type or independent UDT
            std::optional<std::int16_t> schema_path_seqno; // Only return the search_path sequence number if the type is explicitly visible in the search_path
            std::optional<std::uint32_t> array_element_oid; // NULL if it is not an array
            char type; // 'e' for enum, 'd' for domain, 'c' for composite, 'p' for pseudo-type
            std::int16_t storage_bytes; // Storage size in bytes. Returns -1 if the size is variable-length (like text)
        };
        std::vector<record> types;

        [[nodiscard]] auto begin() const { return types.begin(); }
        [[nodiscard]] auto cbegin() const { return types.cbegin(); }
        [[nodiscard]] auto end() const { return types.end(); }
        [[nodiscard]] auto cend() const { return types.cend(); }

        [[nodiscard]] const void* get_type_id() const noexcept override { return &typeid(state_type); }
    };

    static std::string get_query() noexcept {
        return R"sql(
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
                t.oid >= 16384
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
    }

    static std::unique_ptr<state_type> parse(const resultset_view& result, diagnostics* diag) noexcept {
        state_type state;
        for (std::size_t r = 0; r < result.rows().size(); ++r) {
            std::size_t f = 0;
            state_type::record type{};
            // TODO: This is ugly!
            boost::system::error_code ec = result.parse_field<std::uint32_t>(r, f++, type.type_oid);
            ec = result.parse_field<std::string>(r, f++, type.schema_name);
            ec = result.parse_field<std::string>(r, f++, type.type_name);
            ec = result.parse_field<std::optional<std::string>>(r, f++, type.extension_name);
            ec = result.parse_field<std::optional<std::int16_t>>(r, f++, type.schema_path_seqno);
            ec = result.parse_field<std::optional<std::uint32_t>>(r, f++, type.array_element_oid);
            ec = result.parse_field<char>(r, f++, type.type);
            ec = result.parse_field<std::int16_t>(r, f++, type.storage_bytes);
            state.types.push_back(type);
        }
        return std::make_unique<state_type>(state);
    }
};

}

#endif
