//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_REQUEST_HPP
#define NATIVEPG_REQUEST_HPP

#include <boost/compat/function_ref.hpp>
#include <boost/throw_exception.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <vector>

#include "nativepg/field_traits.hpp"
#include "nativepg/protocol/close.hpp"
#include "nativepg/protocol/flush.hpp"
#include "nativepg/protocol/format_codes.hpp"
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

template <serializable_field... Params>
struct statement
{
    std::string name;
};

namespace detail {

template <serializable_field T>
std::error_code do_field_serialize(const T& value, protocol::format_code code, std::vector<unsigned char>& to)
{
    if (code == protocol::format_code::binary)
        return serialize_field_traits<T>::serialize_binary(value, to);
    else
        return serialize_field_traits<T>::serialize_text(value, to);
}

template <serializable_field... Params>
inline constexpr std::array<std::int32_t, sizeof...(Params)> type_oids_for{{field_serialize_oid<Params>...}};

}  // namespace detail

template <serializable_field T>
protocol::serializable_ref make_serializable_ref(const T* value)
{
    // Required for C-array parameters to work (and hence string literals)
    using decayed_type = std::decay_t<const T&>;

    // TODO: nullness check
    return boost::compat::function_ref<std::error_code(protocol::format_code, std::vector<unsigned char>&)>{
        boost::compat::nontype<detail::do_field_serialize<decayed_type>>,
        *value
    };
}

// Type-erases each parameter into a serializable_ref.
// The returned refs point into params, so the pointees
// must outlive the returned array
template <serializable_field... Params>
std::array<protocol::serializable_ref, sizeof...(Params)> make_serializable_refs(const Params&... params)
{
    return {{make_serializable_ref(&params)...}};
}

// TODO: a clear method is missing
class request
{
    std::vector<unsigned char> buffer_;
    std::vector<request_message_type> types_;
    bool autosync_;

    void check(std::error_code ec)
    {
        // TODO: move to compiled
        // TODO: source loc
        if (ec)
            BOOST_THROW_EXCEPTION(std::system_error(ec));
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

    struct add_query_args
    {
        protocol::format_codes param_format = protocol::format_code::binary;
        protocol::format_codes result_format = protocol::format_code::text;
        std::int32_t max_num_rows = 0;
        std::string_view statement_name = {};
        std::string_view portal_name = {};
    };

    // Adds a query with parameters using the extended protocol (PQsendQueryParams)
    template <serializable_field... Params>
    request& add_query(std::string_view q, const Params&... params)
    {
        return add_query(q, add_query_args{}, params...);
    }

    template <serializable_field... Params>
    request& add_query(std::string_view q, const add_query_args& args, const Params&... params)
    {
        return add_query(q, make_serializable_refs(params...), detail::type_oids_for<Params...>, args);
    }

    request& add_query(
        std::string_view q,
        std::span<const protocol::serializable_ref> params,
        std::span<const std::int32_t> param_type_oids,
        const add_query_args& args
    );

    // Prepares a named statement (PQsendPrepare)
    request& add_prepare(
        std::string_view query,
        std::string_view statement_name,
        std::span<const std::int32_t> parameter_type_oids = {}
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

    template <serializable_field... Params>
    request& add_prepare(std::string_view query, const statement<Params...>& stmt)
    {
        return add_prepare(query, stmt.name, detail::type_oids_for<Params...>);
    }

    struct add_execute_args
    {
        protocol::format_codes param_format = protocol::format_code::binary;
        protocol::format_codes result_format = protocol::format_code::text;
        std::int32_t max_num_rows = 0;
        std::string_view portal_name = {};
    };

    // Executes a named prepared statement (PQsendQueryPrepared)
    template <serializable_field... Params>
    request& add_execute(const statement<Params...>& stmt, const std::type_identity_t<Params>&... params)
    {
        return add_execute(stmt, add_execute_args{}, params...);
    }

    template <serializable_field... Params>
    request& add_execute(
        const statement<Params...>& stmt,
        const add_execute_args& args,
        const std::type_identity_t<Params>&... params
    )
    {
        return add_execute(stmt.name, make_serializable_refs(params...), args);
    }

    request& add_execute(
        std::string_view statement_name,
        std::span<const protocol::serializable_ref> params,
        const add_execute_args& args
    );

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

    request& add_sync() { return add(protocol::sync{}); }

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
