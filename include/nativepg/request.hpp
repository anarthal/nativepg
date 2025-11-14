//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_REQUEST_HPP
#define NATIVEPG_REQUEST_HPP

#include <boost/system/detail/error_code.hpp>
#include <boost/system/system_error.hpp>
#include <boost/throw_exception.hpp>

#include <algorithm>
#include <initializer_list>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>

#include "protocol/bind.hpp"
#include "protocol/common.hpp"
#include "protocol/describe.hpp"
#include "protocol/execute.hpp"
#include "protocol/parse.hpp"
#include "protocol/query.hpp"
#include "protocol/sync.hpp"

namespace nativepg {

// TODO: dummy, remove later
template <class T>
struct supports_binary : std::false_type
{
};

template <class T>
void serialize_text(const T&, std::vector<unsigned char>& to);

template <class T>
void serialize_binary(const T&, std::vector<unsigned char>& to);

// TODO: we can optimize this to avoid type-erasing ints and strings
class parameter_ref
{
    using serialize_fn = void (*)(const void* param, std::vector<unsigned char>& buffer);

    template <class T>
    static void do_serialize_text(const void* param, std::vector<unsigned char>& buffer)
    {
        serialize_text(*static_cast<const T*>(param), buffer);
    }

    template <class T>
    static void do_serialize_binary(const void* param, std::vector<unsigned char>& buffer)
    {
        serialize_binary(*static_cast<const T*>(param), buffer);
    }

    template <class T>
    static serialize_fn make_serialize_binary()
    {
        if constexpr (supports_binary<T>::value)
            return &do_serialize_binary<T>;
        else
            return nullptr;
    }

    const void* value_;
    serialize_fn text_;
    serialize_fn binary_;

public:
    template <class T, class = typename std::enable_if_t<!std::is_same_v<T, parameter_ref>>>
    parameter_ref(const T& value) noexcept
        : value_(&value), text_(&do_serialize_text<T>), binary_(make_serialize_binary<T>())
    {
    }

    // TODO: hide this
    bool support_binary() const { return binary_ != nullptr; }

    void text(std::vector<unsigned char>& buffer) { text_(value_, buffer); }
    void binary(std::vector<unsigned char>& buffer) { binary_(value_, buffer); }
};

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
        // Parse
        add_advanced(
            protocol::parse_t{
                .statement_name = {},
                .query = q,
                .parameter_type_oids = {},
            }
        );

        // Bind
        // If all parameters support binary, do binary. Otherwise, do text
        // TODO: provide a way to override this?
        bool use_binary = std::all_of(params.begin(), params.end(), [](parameter_ref p) {
            return p.support_binary();
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
                            param.binary(buffer_);
                        else
                            param.text(buffer_);
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

        // TODO: this doesn't add a sync at the end - how can we ensure it gets added?
    }

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
