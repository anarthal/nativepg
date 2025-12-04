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
}

class request
{
    std::vector<unsigned char> buffer_;
    std::vector<detail::request_msg_type> types_;

    void check(boost::system::error_code ec)
    {
        // TODO: move to compiled
        // TODO: source loc
        if (ec)
            BOOST_THROW_EXCEPTION(boost::system::system_error(ec));
    }

    void add_prepare_raw(
        std::string_view query,
        std::string_view statement_name,
        boost::span<const std::int32_t> parameter_type_oids = {}
    )
    {
        add_advanced(
            protocol::parse_t{
                .statement_name = statement_name,
                .query = query,
                .parameter_type_oids = parameter_type_oids,
            }
        );
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

    // Adds a simple query (PQsendQuery)
    request& add_simple_query(std::string_view q) { return add_advanced(protocol::query{q}); }

    // Adds a query with parameters using the extended protocol (PQsendQueryParams)
    request& add_query(
        std::string_view q,
        std::initializer_list<parameter_ref> params,
        protocol::format_code result_codes
    )
    {
        return add_query(q, std::span<const parameter_ref>(params.begin(), params.end()), result_codes);
    }

    // Adds a query with parameters using the extended protocol (PQsendQueryParams)
    request& add_query(
        std::string_view q,
        std::span<const parameter_ref> params,
        protocol::format_code result_codes
    )
    {
        add_prepare_raw(q, std::string_view{});
        add_execute(std::string_view{}, params, result_codes);
        return *this;
    }

    // Prepares a named statement (PQsendPrepare)
    request& add_prepare(
        std::string_view query,
        std::string_view statement_name,
        boost::span<const std::int32_t> parameter_type_oids = {}
    )
    {
        add_prepare_raw(query, statement_name, parameter_type_oids);
        add_sync();
        return *this;
    }

    // Executes a named prepared statement (PQsendQueryPrepared)
    request& add_execute(
        std::string_view statement_name,
        std::span<const parameter_ref> params,
        protocol::format_code result_codes
    );

    // Describes a named prepared statement (PQsendDescribePrepared)
    request& add_describe_statement(std::string_view statement_name)
    {
        add_advanced(protocol::describe{protocol::portal_or_statement::statement, statement_name});
        return add_sync();
    }

    // Describes a named portal (PQsendDescribePortal)
    request& add_describe_portal(std::string_view portal_name)
    {
        add_advanced(protocol::describe{protocol::portal_or_statement::portal, portal_name});
        return add_sync();
    }

    // Closes a named prepared statement (PQsendClosePrepared)
    request& add_close_statement(std::string_view statement_name)
    {
        add_advanced(protocol::close{protocol::portal_or_statement::statement, statement_name});
        return add_sync();
    }

    // Closes a named portal (PQsendClosePortal)
    request& add_close_portal(std::string_view portal_name)
    {
        add_advanced(protocol::close{protocol::portal_or_statement::portal, portal_name});
        return add_sync();
    }

    // Low-level. Adds a sync message (PQsendPipelineSync)
    request& add_sync() { return add_advanced(protocol::sync{}); }

    // Low-level. Adds a bind message
    request& add_bind(
        std::string_view statement_name,
        std::span<const parameter_ref> params,
        protocol::format_code result_codes
    );

    // Low-level
    request& add_advanced(const protocol::bind& value)
    {
        return add_advanced_impl(value, detail::request_msg_type::bind);
    }

    request& add_advanced(const protocol::close& value)
    {
        return add_advanced_impl(value, detail::request_msg_type::close);
    }

    request& add_advanced(const protocol::describe& value)
    {
        return add_advanced_impl(value, detail::request_msg_type::describe);
    }

    request& add_advanced(const protocol::execute& value)
    {
        return add_advanced_impl(value, detail::request_msg_type::execute);
    }

    request& add_advanced(protocol::flush value)
    {
        return add_advanced_impl(value, detail::request_msg_type::flush);
    }

    request& add_advanced(const protocol::parse_t& value)
    {
        return add_advanced_impl(value, detail::request_msg_type::parse);
    }

    request& add_advanced(protocol::query value)
    {
        return add_advanced_impl(value, detail::request_msg_type::query);
    }

    request& add_advanced(protocol::sync value)
    {
        return add_advanced_impl(value, detail::request_msg_type::sync);
    }
};

}  // namespace nativepg

#endif
