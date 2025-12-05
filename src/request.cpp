//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/container/small_vector.hpp>

#include <algorithm>
#include <cstdint>
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
    protocol::format_code result_codes
)
{
    // See if we should use text or binary
    bool use_binary = std::all_of(params.begin(), params.end(), [](parameter_ref p) {
        return detail::parameter_ref_access::supports_binary(p);
    });

    // Determine the parameter OIDs. If we are to use binary,
    // we need to send these.
    boost::container::small_vector<std::int32_t, 128u> oids;
    if (use_binary)
    {
        oids.reserve(params.size());
        for (const auto& p : params)
            oids.push_back(detail::parameter_ref_access::type_oid(p));
    }

    add_advanced(protocol::parse_t{.statement_name = {}, .query = q, .parameter_type_oids = oids});

    add_advanced(
        protocol::bind{
            .portal_name = {},
            .statement_name = {},
            .parameter_fmt_codes = use_binary ? protocol::format_code::binary : protocol::format_code::text,
            .parameters_fn =
                [params, use_binary, this](protocol::bind_context& ctx) {
                    for (const parameter_ref& param : params)
                    {
                        // TODO: this is bypassing the bind_context API, review
                        ctx.start_parameter();
                        if (use_binary)
                            detail::parameter_ref_access::serialize_binary(param, buffer_);
                        else
                            detail::parameter_ref_access::serialize_text(param, buffer_);
                    }
                },
            .result_fmt_codes = result_codes,
        }
    );

    add_advanced(protocol::describe{protocol::portal_or_statement::portal, {}});

    add_advanced(
        protocol::execute{
            .portal_name = {},
            .max_num_rows = 0,
        }
    );

    return add_sync();
}

request& request::add_execute(
    std::string_view statement_name,
    std::span<const parameter_ref> params,
    protocol::format_code result_codes
)
{
    // Bind. In this version we stick to text because
    // we don't know whether the user sent the required type OIDs in parse
    add_advanced(
        protocol::bind{
            .portal_name = {},
            .statement_name = statement_name,
            .parameter_fmt_codes = protocol::format_code::text,
            .parameters_fn =
                [params, this](protocol::bind_context& ctx) {
                    for (const parameter_ref& param : params)
                    {
                        ctx.start_parameter();
                        detail::parameter_ref_access::serialize_text(param, buffer_);
                    }
                },
            .result_fmt_codes = result_codes,
        }
    );

    // Describe
    add_advanced(protocol::describe{protocol::portal_or_statement::portal, {}});

    // Execute
    add_advanced(
        protocol::execute{
            .portal_name = {},
            .max_num_rows = 0,
        }
    );

    return add_sync();
}