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
#include "protocol/bind.hpp"
#include "protocol/common.hpp"
#include "protocol/describe.hpp"
#include "protocol/execute.hpp"
#include "protocol/parse.hpp"
#include "protocol/query.hpp"
#include "protocol/sync.hpp"

namespace nativepg {

class request
{
    std::vector<unsigned char> buffer_;

    void check(boost::system::error_code ec)
    {
        // TODO: move to compiled
        // TODO: source loc
        if (ec)
            BOOST_THROW_EXCEPTION(boost::system::system_error(ec));
    }

public:
    request() = default;

    // TODO: chaining

    // Adds a simple query (PQsendQuery)
    void add_simple_query(std::string_view q) { add_advanced(protocol::query{q}); }

    // Adds a query with parameters using the extended protocol (PQsendQueryParams)
    void add_query(
        std::string_view q,
        std::initializer_list<parameter_ref> params,
        protocol::format_code result_codes
    )
    {
        add_query(q, std::span<const parameter_ref>(params.begin(), params.end()), result_codes);
    }

    // Adds a query with parameters using the extended protocol (PQsendQueryParams)
    void add_query(
        std::string_view q,
        std::span<const parameter_ref> params,
        protocol::format_code result_codes
    )
    {
        add_prepare(q, std::string_view{});
        add_execute(std::string_view{}, params, result_codes);
    }

    // Prepares a named statement (PQsendPrepare)
    void add_prepare(
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

    // Executes a named prepared statement (PQsendQueryPrepared)
    void add_execute(
        std::string_view statement_name,
        std::span<const parameter_ref> params,
        protocol::format_code result_codes
    );

    // TODO: how can we ensure that syncs are added without creating footguns?
    void add_sync() { add_advanced(protocol::sync{}); }

    // Low-level. TODO: restrict types
    template <class T>
    void add_advanced(const T& v)
    {
        check(protocol::serialize(v, buffer_));
    }
};

}  // namespace nativepg

#endif
