//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/container/small_vector.hpp>

#include <cstdint>
#include <span>
#include <string_view>

#include "nativepg/parameter_ref.hpp"
#include "nativepg/protocol/common.hpp"
#include "nativepg/protocol/describe.hpp"
#include "nativepg/protocol/parse.hpp"
#include "nativepg/request.hpp"

using namespace nativepg;

request& request::add_query(
    std::string_view q,
    std::span<const parameter_ref> params,
    protocol::format_code param_format,
    protocol::format_code result_format,
    std::int32_t max_num_rows
)
{
    // Determine the parameter OIDs. These are required if using binary,
    // but we always send them for consistency
    boost::container::small_vector<std::int32_t, 128u> oids;
    oids.reserve(params.size());
    for (const auto& p : params)
        oids.push_back(p.type_oid());

    // Add the messages
    add(protocol::parse_t{.statement_name = {}, .query = q, .parameter_type_oids = oids});
    add_execute({}, params, param_format, result_format, max_num_rows);

    return *this;
}

request& request::add_execute(
    std::string_view statement_name,
    std::span<const parameter_ref> params,
    protocol::format_code param_format,
    protocol::format_code result_format,
    std::int32_t max_num_rows
)
{
    add_bind(statement_name, params, param_format, {}, result_format);
    add(protocol::describe{protocol::portal_or_statement::portal, {}});
    add(protocol::execute{
        .portal_name = {},
        .max_num_rows = max_num_rows,
    });
    maybe_add_sync();

    return *this;
}

request& request::add_bind(
    std::string_view statement_name,
    std::span<const parameter_ref> params,
    protocol::format_code param_format,
    std::string_view portal_name,
    protocol::format_code result_format
)
{
    return add(
        protocol::bind{
            .portal_name = portal_name,
            .statement_name = statement_name,
            .parameter_fmt_codes = param_format,
            .parameters_fn =
                [params, param_format](protocol::bind_context& ctx) {
                    for (const parameter_ref& param : params)
                    {
                        ctx.start_parameter();
                        if (param_format == protocol::format_code::binary)
                            param.serialize_binary(ctx.buffer());
                        else
                            param.serialize_text(ctx.buffer());
                    }
                },
            .result_fmt_codes = result_format,
        }
    );
}
