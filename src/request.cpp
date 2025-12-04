//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <algorithm>
#include <string_view>

#include "nativepg/protocol/common.hpp"
#include "nativepg/protocol/describe.hpp"
#include "nativepg/request.hpp"

using namespace nativepg;

request& request::add_bind(
    std::string_view statement_name,
    std::span<const parameter_ref> params,
    protocol::format_code result_codes
)
{
    // If all parameters support binary, do binary. Otherwise, do text
    // TODO: provide a way to override this?
    bool use_binary = std::all_of(params.begin(), params.end(), [](parameter_ref p) {
        return detail::parameter_ref_access::supports_binary(p);
    });
    auto fmt_code = use_binary ? protocol::format_code::binary : protocol::format_code::text;
    protocol::bind b{
        .portal_name = {},
        .statement_name = {},
        .parameter_fmt_codes = fmt_code,
        .parameters_fn =
            [params, use_binary, this](protocol::bind_context& ctx) {
                for (parameter_ref param : params)
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
    };

    return add_advanced(b);
}

request& request::add_execute(
    std::string_view statement_name,
    std::span<const parameter_ref> params,
    protocol::format_code result_codes
)
{
    // Bind
    add_bind(statement_name, params, result_codes);

    // Describe
    add_advanced(protocol::describe{protocol::portal_or_statement::portal, std::string_view{}});

    // Execute
    add_advanced(
        protocol::execute{
            .portal_name = {},
            .max_num_rows = 0,
        }
    );

    return add_sync();
}