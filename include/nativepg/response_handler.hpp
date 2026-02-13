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
#include <cstddef>

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
#include "nativepg/request.hpp"

namespace nativepg {

class diagnostics;

// Not an actual message, but a placeholder type to signal
// that the corresponding message was skipped due to a previous error
struct message_skipped
{
};

// TODO: maybe make this a class
using any_request_message = boost::variant2::variant<
    protocol::bind_complete,
    protocol::close_complete,
    protocol::command_complete,
    protocol::data_row,
    protocol::parameter_description,
    protocol::row_description,
    protocol::empty_query_response,
    protocol::portal_suspended,
    protocol::error_response,
    protocol::parse_complete,
    message_skipped>;

// TODO: improve API
struct handler_setup_result
{
    boost::system::error_code ec;
    std::size_t offset{};

    handler_setup_result(boost::system::error_code ec) noexcept : ec(ec) {}
    handler_setup_result(std::size_t offset) noexcept : offset(offset) {}

    friend bool operator==(const handler_setup_result&, const handler_setup_result&) = default;
};

template <class T>
concept response_handler = requires(
    T& handler,
    const request& req,
    const any_request_message& msg,
    std::size_t offset
) {
    { handler.setup(req, offset) } -> std::convertible_to<handler_setup_result>;
    { handler.on_message(msg, offset) };
    { handler.result() } -> std::same_as<const extended_error&>;
};

// Type-erased reference to a response handler
class response_handler_ref
{
    using setup_fn = handler_setup_result (*)(void*, const request&, std::size_t);
    using on_message_fn = void (*)(void*, const any_request_message&, std::size_t);
    using result_fn = const extended_error& (*)(const void*);

    void* obj_;
    setup_fn setup_;
    on_message_fn on_message_;
    result_fn result_;

    template <class T>
    static handler_setup_result do_setup(void* obj, const request& req, std::size_t offset)
    {
        return static_cast<T*>(obj)->setup(req, offset);
    }

    template <class T>
    static void do_on_message(void* obj, const any_request_message& msg, std::size_t offset)
    {
        static_cast<T*>(obj)->on_message(msg, offset);
    }

    template <class T>
    static const extended_error& do_result(const void* obj)
    {
        return static_cast<const T*>(obj)->result();
    }

public:
    template <response_handler T>
        requires(!std::same_as<std::remove_cvref_t<T>, response_handler_ref>)
    response_handler_ref(T& obj) noexcept
        : obj_(&obj), setup_(&do_setup<T>), on_message_(&do_on_message<T>), result_(&do_result<T>)
    {
    }

    handler_setup_result setup(const request& req, std::size_t offset) { return setup_(obj_, req, offset); }
    void on_message(const any_request_message& req, std::size_t offset)
    {
        return on_message_(obj_, req, offset);
    }
    const extended_error& result() const { return result_(obj_); }
};




}  // namespace nativepg

#endif
