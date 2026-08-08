//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_REQUEST_HPP
#define NATIVEPG_REQUEST_HPP

#include <boost/compat/detail/nontype.hpp>
#include <boost/compat/function_ref.hpp>
#include <boost/throw_exception.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

#include "nativepg/field_traits.hpp"
#include "nativepg/protocol/close.hpp"
#include "nativepg/protocol/flush.hpp"
#include "nativepg/protocol/format_codes.hpp"
#include "nativepg/protocol/views.hpp"
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
protocol::serializable_ref to_serializable_ref(const T* value, bool use_binary)
{
    // Required for C-array parameters to work (and hence string literals)
    using decayed_type = std::decay_t<const T&>;

    // TODO: nullness check
    if (use_binary)
    {
        return boost::compat::function_ref<std::error_code(std::vector<unsigned char>&)>{
            boost::compat::nontype<field_serialize_binary<decayed_type>>,
            *value
        };
    }
    else
    {
        return boost::compat::function_ref<std::error_code(std::vector<unsigned char>&)>{
            boost::compat::nontype<field_serialize_text<decayed_type>>,
            *value
        };
    }
}

// Retrieves the format code to apply to the parameter at the given index.
// Precondition: if codes is a list, idx is in range. check_format_codes_size enforces this
inline protocol::format_code format_code_for(protocol::format_codes codes, std::size_t idx)
{
    switch (codes.type())
    {
        case protocol::format_codes::kind::all_text: return protocol::format_code::text;
        case protocol::format_codes::kind::all_binary: return protocol::format_code::binary;
        case protocol::format_codes::kind::list:
            BOOST_ASSERT(idx < codes.get_list().size());
            return codes.get_list()[idx];
        default: BOOST_ASSERT(false); return protocol::format_code::text;
    }
}

// Throws std::invalid_argument if codes is a list with a size other than num_params.
// Single-code kinds apply to every parameter, so they always match
void check_format_codes_size(protocol::format_codes codes, std::size_t num_params);

template <std::size_t... I, serializable_field... Params>
std::array<protocol::serializable_ref, sizeof...(Params)> to_serializable_refs_impl(
    std::index_sequence<I...>,
    protocol::format_codes codes,
    const Params*... params
)
{
    return {{to_serializable_ref(params, format_code_for(codes, I) == protocol::format_code::binary)...}};
}

// Type-erases each parameter into a serializable_ref, using the format code that
// corresponds to its position. The returned refs point into *params, so the pointees
// must outlive the returned array
template <serializable_field... Params>
std::array<protocol::serializable_ref, sizeof...(Params)> to_serializable_refs(
    protocol::format_codes codes,
    const Params*... params
)
{
    // Validate the number of format codes once, rather than once per parameter
    check_format_codes_size(codes, sizeof...(Params));

    return to_serializable_refs_impl(std::index_sequence_for<Params...>{}, codes, params...);
}

template <serializable_field... Params>
inline constexpr std::array<std::int32_t, sizeof...(Params)> type_oids_for{{field_serialize_oid<Params>...}};

}  // namespace detail

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
        return add_query(
            q,
            detail::to_serializable_refs(args.param_format, &params...),
            detail::type_oids_for<Params...>,
            args
        );
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

    // Prepares a named statement (PQsendPrepare)
    template <serializable_field... Params>
    request& add_prepare(std::string_view query, const statement<Params...>& stmt)
    {
        return add_prepare(query, stmt.name, detail::type_oids_for<Params...>);
    }

    // Executes a named prepared statement (PQsendQueryPrepared)
    // Parameter format defaults to text because binary requires sending
    // type OIDs in prepare, and we're not sure if the user did it
    struct add_execute_args
    {
        protocol::format_codes param_format = protocol::format_code::binary;
        protocol::format_codes result_format = protocol::format_code::text;
        std::int32_t max_num_rows = 0;
        std::string_view portal_name = {};
    };

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
        return add_execute(stmt.name, detail::to_serializable_refs(args.param_format, &params...), args);
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
