//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_REQUEST_HPP
#define NATIVEPG_REQUEST_HPP

#include <boost/system/error_code.hpp>
#include <boost/system/system_error.hpp>
#include <boost/throw_exception.hpp>

#include <algorithm>
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

    void add_query(std::string_view q);

    // TODO: allow an array of format codes?
    void add_query(
        std::string_view q,
        std::initializer_list<parameter_ref> params,
        protocol::format_code result_codes
    )
    {
        add_query(q, std::span<const parameter_ref>(params.begin(), params.end()), result_codes);
    }

    // TODO: move to compiled
    void add_query(
        std::string_view q,
        std::span<const parameter_ref> params,
        protocol::format_code result_codes
    )
    {
        add_prepare(q, {});
        add_execute({}, params, result_codes);
    }

    void add_prepare(std::string_view query, std::string_view statement_name)
    {
        // Prepare is just a parse
        add_advanced(
            protocol::parse_t{
                .statement_name = statement_name,
                .query = query,
                .parameter_type_oids = {},
            }
        );
    }

    void add_execute(
        std::string_view statement_name,
        std::span<const parameter_ref> params,
        protocol::format_code result_codes
    )
    {
        // Bind
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
        add_advanced(b);

        // Describe
        add_advanced(
            protocol::describe{
                .type = protocol::portal_or_statement::portal,
                .name = {},
            }
        );

        // Execute
        add_advanced(
            protocol::execute{
                .portal_name = {},
                .max_num_rows = 0,
            }
        );
    }

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
