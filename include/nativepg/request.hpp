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
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string_view>
#include <type_traits>
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

namespace detail {
enum class request_msg_type
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
struct request_access;
}  // namespace detail

template <class... Params>
struct statement
{
    std::string name;
};

class request
{
    std::vector<unsigned char> buffer_;
    std::vector<detail::request_msg_type> types_;

    friend struct detail::request_access;

    void check(boost::system::error_code ec)
    {
        // TODO: move to compiled
        // TODO: source loc
        if (ec)
            BOOST_THROW_EXCEPTION(boost::system::system_error(ec));
    }

    template <class T>
    request& add_advanced_impl(const T& value, detail::request_msg_type type)
    {
        types_.reserve(types_.size() + 1u);  // strong guarantee
        check(protocol::serialize(value, buffer_));
        types_.push_back(type);
        return *this;
    }

public:
    request() = default;

    // Returns the serialized payload
    boost::span<const unsigned char> payload() const { return buffer_; }

    // Adds a simple query (PQsendQuery)
    request& add_simple_query(std::string_view q) { return add(protocol::query{q}); }

    // Adds a query with parameters using the extended protocol (PQsendQueryParams)
    request& add_query(
        std::string_view q,
        std::initializer_list<parameter_ref> params,
        protocol::format_code result_codes = protocol::format_code::text
    )
    {
        return add_query(q, std::span<const parameter_ref>(params.begin(), params.end()), result_codes);
    }

    // Adds a query with parameters using the extended protocol (PQsendQueryParams)
    request& add_query(
        std::string_view q,
        std::span<const parameter_ref> params,
        protocol::format_code result_codes = protocol::format_code::text
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
        return add(protocol::sync{});
    }

    // Prepares a named statement (PQsendPrepare)
    template <class... Params>
    request& add_prepare(std::string_view query, const statement<Params...>& stmt)
    {
        std::array<std::int32_t, sizeof...(Params)> type_oids{{detail::parameter_type_oid<Params>::value...}};
        return add_prepare(query, stmt.name, type_oids);
    }

    // Executes a named prepared statement (PQsendQueryPrepared)
    request& add_execute(
        std::string_view statement_name,
        std::span<const parameter_ref> params,
        protocol::format_code result_codes = protocol::format_code::text
    );

    request& add_execute(
        std::string_view statement_name,
        std::initializer_list<parameter_ref> params,
        protocol::format_code result_codes = protocol::format_code::text
    )
    {
        return add_execute(statement_name, std::span<const parameter_ref>(params), result_codes);
    }

    // Executes a named prepared statement (PQsendQueryPrepared)
    template <class... Params>
    request& add_execute(
        const statement<Params...>& stmt,
        const std::type_identity_t<Params>&... params,
        protocol::format_code result_codes = protocol::format_code::text
    )
    {
        std::array<parameter_ref, sizeof...(Params)> erased_params{{params...}};
        return add_execute(stmt.name, erased_params, result_codes);
    }

    // Describes a named prepared statement (PQsendDescribePrepared)
    request& add_describe_statement(std::string_view statement_name)
    {
        add(protocol::describe{protocol::portal_or_statement::statement, statement_name});
        return add(protocol::sync{});
    }

    // Describes a named portal (PQsendDescribePortal)
    request& add_describe_portal(std::string_view portal_name)
    {
        add(protocol::describe{protocol::portal_or_statement::portal, portal_name});
        return add(protocol::sync{});
    }

    // Closes a named prepared statement (PQsendClosePrepared)
    request& add_close_statement(std::string_view statement_name)
    {
        add(protocol::close{protocol::portal_or_statement::statement, statement_name});
        return add(protocol::sync{});
    }

    // Closes a named portal (PQsendClosePortal)
    request& add_close_portal(std::string_view portal_name)
    {
        add(protocol::close{protocol::portal_or_statement::portal, portal_name});
        return add(protocol::sync{});
    }

    // Low-level
    request& add(const protocol::bind& value)
    {
        return add_advanced_impl(value, detail::request_msg_type::bind);
    }

    request& add(const protocol::close& value)
    {
        return add_advanced_impl(value, detail::request_msg_type::close);
    }

    request& add(const protocol::describe& value)
    {
        return add_advanced_impl(value, detail::request_msg_type::describe);
    }

    request& add(const protocol::execute& value)
    {
        return add_advanced_impl(value, detail::request_msg_type::execute);
    }

    request& add(protocol::flush value) { return add_advanced_impl(value, detail::request_msg_type::flush); }

    request& add(const protocol::parse_t& value)
    {
        return add_advanced_impl(value, detail::request_msg_type::parse);
    }

    request& add(protocol::query value) { return add_advanced_impl(value, detail::request_msg_type::query); }

    request& add(protocol::sync value) { return add_advanced_impl(value, detail::request_msg_type::sync); }
};

namespace detail {
struct request_access
{
    static boost::span<const request_msg_type> messages(const request& r) { return r.types_; }
};
}  // namespace detail

}  // namespace nativepg

#endif
