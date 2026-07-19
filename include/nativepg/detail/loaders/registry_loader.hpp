//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_LOADERS_REGISTRY_LOADER_HPP
#define NATIVEPG_LOADERS_REGISTRY_LOADER_HPP

#include <memory>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#include "nativepg/protocol/detail/exec_fsm.hpp"
#include "nativepg/protocol/startup_fsm.hpp"
#include "nativepg/request.hpp"
#include "nativepg/response.hpp"
#include "nativepg/response_handler.hpp"
#include "nativepg/types/registry.hpp"

namespace nativepg::detail::loaders {

template <class ConnImpl>
struct load_op
{
    ConnImpl& impl;
    std::unique_ptr<request> req_;
    std::unique_ptr<types::type_registry> reg_;
    // result_ is heap-allocated (rather than an inline member) because handler_ keeps a reference to it,
    // and this operation object is moved every time it is resumed: boost::asio::composed_op moves its
    // whole implementation on every operator() re-invocation (e.g. when passed to async_write /
    // async_read_some as std::move(self)), not just once at initiation. An inline vector member would
    // leave handler_'s reference dangling after the very first resumption.
    std::unique_ptr<std::vector<types::type_info>> result_;
    std::unique_ptr<resultset_callback_t<types::type_info, nativepg::detail::into_handler<types::type_info>>>
        handler_;
    protocol::detail::exec_fsm fsm_;

    load_op(ConnImpl& impl, std::string_view sql)
        : impl(impl),
          req_(std::make_unique<request>()),
          result_(std::make_unique<std::vector<types::type_info>>()),
          handler_(
              std::make_unique<
                  resultset_callback_t<types::type_info, nativepg::detail::into_handler<types::type_info>>>(
                  nativepg::detail::into_handler<types::type_info>{*result_}
              )
          ),
          fsm_(req_.get(), response_handler_ref{handler_.get()})
    {
        req_->add_query(sql, {});
    }

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
            case protocol::startup_fsm::result_type::done:
                reg_ = std::make_unique<types::type_registry>(*result_);
                self.complete(fsm_.get_result(res.error()), std::move(*reg_));
                break;
            default: BOOST_ASSERT(false);
        }
    }
};

class type_registry_loader
{
    inline constexpr static std::string_view sql_statement = R"sql(
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

public:
    template <
        class ConnImpl,
        boost::asio::completion_token_for<void(extended_error, types::type_registry)> CompletionToken =
            boost::asio::deferred_t>
    static auto async_load(ConnImpl& conn_impl, CompletionToken&& token = {})
    {
        return boost::asio::async_compose<CompletionToken, void(extended_error, types::type_registry)>(
            load_op<ConnImpl>{conn_impl, sql_statement},
            token,
            conn_impl.sock
        );
    }
};

}  // namespace nativepg::detail::loaders

#endif  // NATIVEPG_LOADERS_REGISTRY_LOADER_HPP
