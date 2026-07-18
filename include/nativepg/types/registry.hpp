//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_TYPES_REGISTRY_HPP
#define NATIVEPG_TYPES_REGISTRY_HPP

#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#include "nativepg/response.hpp"

namespace nativepg {

namespace detail {
class connection_impl;
}  // namespace detail

namespace types {

namespace detail {

// Drives an exec_fsm over a connection_impl, mirroring nativepg::detail::exec_op,
// without depending on connection.hpp (avoids a circular include).
// Templated on ConnImpl (rather than hardcoding nativepg::detail::connection_impl,
// which is only forward-declared in this header) so that member access to
// impl.st / impl.sock is a dependent expression, deferring completeness checks
// to instantiation time (performed from connection.hpp, where connection_impl
// is fully defined).
template <class ConnImpl>
struct load_op
{
    ConnImpl& impl;
    protocol::detail::exec_fsm fsm_;

    template <class Self>
    void operator()(Self& self, boost::system::error_code ec = {}, std::size_t bytes_transferred = {})
    {
        auto res = fsm_.resume(impl.st, ec, bytes_transferred);
        switch (res.type())
        {
            case protocol::startup_fsm::result_type::write:
                boost::asio::async_write(impl.sock, res.write_data(), std::move(self));
                break;
            case protocol::startup_fsm::result_type::read:
                impl.sock.async_read_some(res.read_buffer(), std::move(self));
                break;
            case protocol::startup_fsm::result_type::done: self.complete(fsm_.get_result(res.error())); break;
            default: BOOST_ASSERT(false);
        }
    }
};

}  // namespace detail
struct type_info
{
    std::uint32_t type_oid;
    std::string schema_name;
    std::string type_name;
    std::string extension_name;
    short schema_path_seqno;
    std::uint32_t array_element_oid;
    char type;
    short storage_bytes;
};
BOOST_DESCRIBE_STRUCT(
    type_info,
    (),
    (type_oid,
     schema_name,
     type_name,
     extension_name,
     schema_path_seqno,
     array_element_oid,
     type,
     storage_bytes)
);

class type_registry
{
    const std::string_view sql_statement = R"sql(
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
    type_oid,
    schema_name,
    type_name;
        )sql";

    std::vector<type_info> table_;
    request req_;
    resultset_callback_t<type_info, nativepg::detail::into_handler<type_info>> handler_{
        nativepg::detail::into_handler<type_info>{table_}
    };

public:
    type_registry() = default;
    type_registry(const type_registry&) = delete;
    type_registry(type_registry&&) = delete;
    type_registry& operator=(const type_registry&) = delete;
    type_registry& operator=(type_registry&&) = delete;
    ~type_registry() = default;

    template <
        class ConnImpl,
        boost::asio::completion_token_for<void(extended_error)> CompletionToken = boost::asio::deferred_t>
    auto async_load(ConnImpl& conn_impl, CompletionToken&& token = {})
    {
        table_.clear();
        req_ = request{};
        req_.add_query(sql_statement, {});

        auto result = boost::asio::async_compose<CompletionToken, void(extended_error)>(
            detail::load_op<ConnImpl>{
                conn_impl,
                protocol::detail::exec_fsm{&req_, response_handler_ref{&handler_}}
        },
            token,
            conn_impl.sock
        );

        return result;
    }

    // Range support, so instances can be iterated
    auto begin() const { return table_.begin(); }
    auto end() const { return table_.end(); }
    std::size_t size() const { return table_.size(); }
    bool empty() const { return table_.empty(); }

    const type_info& find_by_oid(std::uint32_t oid) const
    {
        auto it = std::ranges::find_if(table_, [oid](const type_info& info) { return info.type_oid == oid; });
        return *it;
    }

    const type_info& find_by_type_name(const std::string& schema_name, const std::string& type_name) const
    {
        auto it = std::ranges::find_if(table_, [&schema_name, &type_name](const type_info& info) {
            return info.schema_name == schema_name && info.type_name == type_name;
        });
        return *it;
    }

    const type_info& find_by_type_name(const std::string& type_name) const
    {
        auto it = std::ranges::find_if(table_, [&type_name](const type_info& info) {
            return info.type_name == type_name;
        });
        return *it;
    }
};

}  // namespace types
}  // namespace nativepg

#endif  // NATIVEPG_TYPES_REGISTRY_HPP
