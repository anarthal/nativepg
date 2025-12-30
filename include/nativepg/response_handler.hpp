//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_RESPONSE_HANDLER_HPP
#define NATIVEPG_RESPONSE_HANDLER_HPP

#include <boost/system/error_code.hpp>
#include <boost/variant2/variant.hpp>

#include <concepts>

#include "nativepg/extended_error.hpp"
#include "nativepg/protocol/bind.hpp"
#include "nativepg/protocol/close.hpp"
#include "nativepg/protocol/command_complete.hpp"
#include "nativepg/protocol/data_row.hpp"
#include "nativepg/protocol/describe.hpp"
#include "nativepg/protocol/empty_query_response.hpp"
#include "nativepg/protocol/execute.hpp"
#include "nativepg/protocol/notice_error.hpp"
#include "nativepg/protocol/parse.hpp"

namespace nativepg {

class diagnostics;

// These are not actual messages, but a placeholder type to signal
// that the corresponding message was skipped due to a previous error
// clang-format off
struct parse_skipped {};
struct bind_skipped {};
struct close_skipped {};
struct execute_skipped {};
struct describe_skipped {};
// clang-format on

// TODO: maybe make this a class
using any_request_message = boost::variant2::variant<
    protocol::bind_complete,
    protocol::close_complete,
    protocol::command_complete,
    protocol::data_row,
    protocol::parameter_description,
    protocol::row_description,
    protocol::no_data,
    protocol::empty_query_response,
    protocol::portal_suspended,
    protocol::error_response,
    protocol::parse_complete,
    parse_skipped,
    bind_skipped,
    close_skipped,
    execute_skipped,
    describe_skipped>;

template <class T>
concept response_handler = requires(T& handler, const any_request_message& msg, diagnostics& diag) {
    { handler.on_message(msg) };
    { handler.result() } -> std::convertible_to<extended_error>;
};

enum class handler_status
{
    needs_more,
    done,
};

// Type-erased reference to a response handler
class response_handler_ref
{
    using on_message_fn = handler_status (*)(void*, const any_request_message&);
    using result_fn = extended_error (*)(const void*);

    void* obj_;
    on_message_fn on_message_;
    result_fn result_;

    template <class T>
    static handler_status do_on_message(void* obj, const any_request_message& msg)
    {
        return static_cast<T*>(obj)->on_message(msg);
    }

    template <class T>
    static extended_error do_result(const void* obj)
    {
        return static_cast<const T*>(obj)->result();
    }

public:
    template <response_handler T>
        requires(!std::same_as<T, response_handler_ref>)
    response_handler_ref(T& obj) noexcept : obj_(&obj), on_message_(&do_on_message<T>), result_(&do_result<T>)
    {
    }

    handler_status on_message(const any_request_message& req) { return on_message_(obj_, req); }
    extended_error result() const { return result_(obj_); }
};

}  // namespace nativepg

#endif
