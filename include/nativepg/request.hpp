//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_REQUEST_HPP
#define NATIVEPG_REQUEST_HPP

#include <boost/core/span.hpp>
#include <boost/system/error_code.hpp>
#include <boost/system/system_error.hpp>
#include <boost/throw_exception.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string_view>
#include <vector>

#include "nativepg/parameter_ref.hpp"
#include "nativepg/protocol/close.hpp"
#include "nativepg/protocol/flush.hpp"
#include "protocol/bind.hpp"
#include "protocol/common.hpp"
#include "protocol/describe.hpp"
#include "protocol/execute.hpp"
#include "protocol/parse.hpp"
#include "protocol/query.hpp"
#include "protocol/sync.hpp"

namespace nativepg {

enum class request_message_type
{
    bind,
    close,
    describe,
    execute,
    flush,
    parse,
    query,
    sync,
};

template <std::size_t N>
struct bound_statement
{
    std::string_view name;
    std::array<parameter_ref, N> params;
};

template <class... Params>
struct statement
{
    std::string name;

    bound_statement<sizeof...(Params)> bind(const Params&... values) { return {name, {values...}}; }
};

class request
{
    std::vector<unsigned char> buffer_;
    std::vector<request_message_type> types_;
    bool autosync_;

    void check(boost::system::error_code ec)
    {
        // TODO: move to compiled
        // TODO: source loc
        if (ec)
            BOOST_THROW_EXCEPTION(boost::system::system_error(ec));
    }

    template <class T>
    request& add_advanced_impl(const T& value, request_message_type type)
    {
        types_.reserve(types_.size() + 1u);  // strong guarantee
        check(protocol::serialize(value, buffer_));
        types_.push_back(type);
        return *this;
    }

    void maybe_add_sync()
    {
        if (autosync_)
            add(protocol::sync{});
    }

public:
    enum class param_format
    {
        text,         // Use text for all params
        select_best,  // Let the library select what's best, depending on what each parameter supports
    };

    // When autosync is enabled, sync messages are added automatically.
    // You may disable autosync and add syncs manually to achieve certain
    // pipeline patterns. This is an advanced feature, don't use it if you
    // don't know what a sync message is.
    request(bool autosync = true) noexcept : autosync_(autosync) {}

    bool autosync() const { return autosync_; }
    void set_autosync(bool value) { autosync_ = value; }

    // Returns the serialized payload
    std::span<const unsigned char> payload() const { return buffer_; }
    std::span<const request_message_type> messages() const { return types_; }

    // Adds a simple query (PQsendQuery)
    request& add_simple_query(std::string_view q) { return add(protocol::query{q}); }

    // Adds a query with parameters using the extended protocol (PQsendQueryParams)
    request& add_query(
        std::string_view q,
        std::initializer_list<parameter_ref> params = {},
        param_format fmt = param_format::select_best,
        protocol::format_code result_codes = protocol::format_code::text,
        std::int32_t max_num_rows = 0
    )
    {
        return add_query(q, std::span<const parameter_ref>(params), fmt, result_codes, max_num_rows);
    }

    request& add_query(
        std::string_view q,
        std::span<const parameter_ref> params,
        param_format fmt = param_format::select_best,
        protocol::format_code result_codes = protocol::format_code::text,
        std::int32_t max_num_rows = 0
    );

    // Prepares a named statement (PQsendPrepare)
    request& add_prepare(
        std::string_view query,
        std::string_view statement_name,
        boost::span<const std::int32_t> parameter_type_oids = {}
    )
    {
        add(protocol::parse_t{
            .statement_name = statement_name,
            .query = query,
            .parameter_type_oids = parameter_type_oids,
        });
        maybe_add_sync();
        return *this;
    }

    // Prepares a named statement (PQsendPrepare)
    template <class... Params>
    request& add_prepare(std::string_view query, const statement<Params...>& stmt)
    {
        std::array<std::int32_t, sizeof...(Params)> type_oids{{detail::parameter_type_oid<Params>::value...}};
        return add_prepare(query, stmt.name, type_oids);
    }

    // Executes a named prepared statement (PQsendQueryPrepared)
    // Parameter format defaults to text because binary requires sending
    // type OIDs in prepare, and we're not sure if the user did it
    request& add_execute(
        std::string_view statement_name,
        std::initializer_list<parameter_ref> params,
        param_format fmt = param_format::text,
        protocol::format_code result_codes = protocol::format_code::text,
        std::int32_t max_num_rows = 0
    )
    {
        return add_execute(
            statement_name,
            std::span<const parameter_ref>(params),
            fmt,
            result_codes,
            max_num_rows
        );
    }

    request& add_execute(
        std::string_view statement_name,
        std::span<const parameter_ref> params,
        param_format fmt = param_format::text,
        protocol::format_code result_codes = protocol::format_code::text,
        std::int32_t max_num_rows = 0
    );

    // Executes a named prepared statement (PQsendQueryPrepared)
    template <std::size_t N>
    request& add_execute(
        const bound_statement<N>& stmt,
        param_format fmt = param_format::select_best,
        protocol::format_code result_codes = protocol::format_code::text,
        std::int32_t max_num_rows = 0
    )
    {
        return add_execute(stmt.name, stmt.params, fmt, result_codes, max_num_rows);
    }

    // Describes a named prepared statement (PQsendDescribePrepared)
    request& add_describe_statement(std::string_view statement_name)
    {
        add(protocol::describe{protocol::portal_or_statement::statement, statement_name});
        maybe_add_sync();
        return *this;
    }

    // Describes a named portal (PQsendDescribePortal)
    request& add_describe_portal(std::string_view portal_name)
    {
        add(protocol::describe{protocol::portal_or_statement::portal, portal_name});
        maybe_add_sync();
        return *this;
    }

    // Closes a named prepared statement (PQsendClosePrepared)
    request& add_close_statement(std::string_view statement_name)
    {
        add(protocol::close{protocol::portal_or_statement::statement, statement_name});
        maybe_add_sync();
        return *this;
    }

    // Closes a named portal (PQsendClosePortal)
    request& add_close_portal(std::string_view portal_name)
    {
        add(protocol::close{protocol::portal_or_statement::portal, portal_name});
        maybe_add_sync();
        return *this;
    }

    // Low level
    request& add_bind(
        std::string_view statement_name,
        std::initializer_list<const parameter_ref> params,
        param_format fmt = param_format::text,
        std::string_view portal_name = {},
        protocol::format_code result_fmt_codes = protocol::format_code::text
    )
    {
        return add_bind(
            statement_name,
            std::span<const parameter_ref>(params),
            fmt,
            portal_name,
            result_fmt_codes
        );
    }

    request& add_bind(
        std::string_view statement_name,
        std::span<const parameter_ref> params,
        param_format fmt = param_format::text,
        std::string_view portal_name = {},
        protocol::format_code result_fmt_codes = protocol::format_code::text
    );

    template <std::size_t N>
    request& add_bind(
        const bound_statement<N>& stmt,
        param_format fmt = param_format::select_best,
        std::string_view portal_name = {},
        protocol::format_code result_codes = protocol::format_code::text
    )
    {
        return add_bind(stmt.name, stmt.params, fmt, portal_name, result_codes);
    }

    request& add(const protocol::bind& value) { return add_advanced_impl(value, request_message_type::bind); }

    request& add(const protocol::close& value)
    {
        return add_advanced_impl(value, request_message_type::close);
    }

    request& add(const protocol::describe& value)
    {
        return add_advanced_impl(value, request_message_type::describe);
    }

    request& add(const protocol::execute& value)
    {
        return add_advanced_impl(value, request_message_type::execute);
    }

    request& add(protocol::flush value) { return add_advanced_impl(value, request_message_type::flush); }

    request& add(const protocol::parse_t& value)
    {
        return add_advanced_impl(value, request_message_type::parse);
    }

    request& add(protocol::query value) { return add_advanced_impl(value, request_message_type::query); }

    request& add(protocol::sync value) { return add_advanced_impl(value, request_message_type::sync); }
};

}  // namespace nativepg

#endif
