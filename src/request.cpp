//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/container/small_vector.hpp>

#include <algorithm>
#include <cstdint>
#include <span>
#include <string_view>

#include "nativepg/parameter_ref.hpp"
#include "nativepg/protocol/common.hpp"
#include "nativepg/protocol/describe.hpp"
#include "nativepg/protocol/parse.hpp"
#include "nativepg/request.hpp"

using namespace nativepg;

static protocol::format_code compute_format(request::param_format fmt, std::span<const parameter_ref> params)
{
    switch (fmt)
    {
    case request::param_format::select_best:
    {
        bool all_support_binary = std::all_of(params.begin(), params.end(), [](parameter_ref p) {
            return detail::parameter_ref_access::supports_binary(p);
        });
        return all_support_binary ? protocol::format_code::binary : protocol::format_code::text;
    }
    case request::param_format::text:
    default: return protocol::format_code::text;
    }
}

request& request::add_query(
    std::string_view q,
    std::span<const parameter_ref> params,
    param_format fmt,
    protocol::format_code result_codes
)
{
    // Determine the parameter OIDs. These are required if using binary
    boost::container::small_vector<std::int32_t, 128u> oids;
    auto fmt_code = compute_format(fmt, params);
    if (fmt_code == protocol::format_code::binary)
    {
        oids.reserve(params.size());
        for (const auto& p : params)
            oids.push_back(detail::parameter_ref_access::type_oid(p));
    }

    // Add the messages
    add(protocol::parse_t{.statement_name = {}, .query = q, .parameter_type_oids = oids});
    add_bind({}, params, fmt, {}, result_codes);
    add(protocol::describe{protocol::portal_or_statement::portal, {}});
    add(protocol::execute{
        .portal_name = {},
        .max_num_rows = 0,
    });
    maybe_add_sync();

    return *this;
}

request& request::add_execute(
    std::string_view statement_name,
    std::span<const parameter_ref> params,
    param_format fmt,
    protocol::format_code result_codes
)
{
    add_bind(statement_name, params, fmt, {}, result_codes);
    add(protocol::describe{protocol::portal_or_statement::portal, {}});
    add(protocol::execute{
        .portal_name = {},
        .max_num_rows = 0,
    });
    maybe_add_sync();

    return *this;
}

request& request::add_bind(
    std::string_view statement_name,
    std::span<const parameter_ref> params,
    param_format fmt,
    std::string_view portal_name,
    protocol::format_code result_fmt_codes
)
{
    auto fmt_code = compute_format(fmt, params);
    return add(
        protocol::bind{
            .portal_name = portal_name,
            .statement_name = statement_name,
            .parameter_fmt_codes = fmt_code,
            .parameters_fn =
                [params, fmt_code](protocol::bind_context& ctx) {
                    for (const parameter_ref& param : params)
                    {
                        ctx.start_parameter();
                        if (fmt_code == protocol::format_code::binary)
                            detail::parameter_ref_access::serialize_binary(param, ctx.buffer());
                        else
                            detail::parameter_ref_access::serialize_text(param, ctx.buffer());
                    }
                },
            .result_fmt_codes = result_fmt_codes,
        }
    );
}
