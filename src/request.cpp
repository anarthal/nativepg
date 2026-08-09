//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <cstdint>
#include <span>
#include <string_view>

#include "nativepg/protocol/bind.hpp"
#include "nativepg/protocol/common.hpp"
#include "nativepg/protocol/describe.hpp"
#include "nativepg/protocol/parse.hpp"
#include "nativepg/request.hpp"

using namespace nativepg;

request& request::add_query(
    std::string_view q,
    std::span<const protocol::serializable_ref> params,
    std::span<const std::int32_t> param_type_oids,
    const add_query_args& args
)
{
    // Add the messages
    add(protocol::parse_t{
        .statement_name = args.statement_name,
        .query = q,
        .parameter_type_oids = param_type_oids
    });
    add_execute(
        args.statement_name,
        params,
        {
            .param_format = args.param_format,
            .result_format = args.result_format,
            .max_num_rows = args.max_num_rows,
            .portal_name = args.portal_name,
        }
    );

    return *this;
}

request& request::add_execute(
    std::string_view statement_name,
    std::span<const protocol::serializable_ref> params,
    const add_execute_args& args
)
{
    add(protocol::bind{
        .portal_name = args.portal_name,
        .statement_name = statement_name,
        .parameter_fmt_codes = args.param_format,
        .parameters = params,
        .result_fmt_codes = args.result_format,
    });
    add(protocol::describe{
        .type = protocol::portal_or_statement::portal,
        .name = args.portal_name,
    });
    add(protocol::execute{
        .portal_name = args.portal_name,
        .max_num_rows = args.max_num_rows,
    });
    maybe_add_sync();

    return *this;
}
