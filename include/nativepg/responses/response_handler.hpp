//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_RESPONSE_HANDLER_HPP
#define NATIVEPG_RESPONSE_HANDLER_HPP

#include <concepts>
#include <cstddef>
#include <system_error>

#include "nativepg/client_errc.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/request.hpp"

namespace nativepg {

class diagnostics;
class any_request_message;
class context;

// TODO: improve API
struct handler_setup_result
{
    std::error_code ec;
    std::size_t offset{};

    handler_setup_result(std::error_code ec) noexcept : ec(ec) {}
    handler_setup_result(std::size_t offset) noexcept : offset(offset) {}

    friend bool operator==(const handler_setup_result&, const handler_setup_result&) = default;
};

template <class T>
concept response_handler = requires(
    T& handler,
    const any_request_message& msg,
    extended_error& err,
    std::size_t offset
) {
    { handler.on_message(msg, offset, err) };
} &&
     (
         requires(T& handler, const request& req, std::size_t offset, context* ctx) {
             { handler.setup(req, offset, ctx) } -> std::convertible_to<handler_setup_result>;
         } ||
         requires(T& handler, const request& req, std::size_t offset) {
             { handler.setup(req, offset) } -> std::convertible_to<handler_setup_result>;
         }
     );

// Handlers can use the legacy setup signature or accept the connection context.
namespace detail {

template <class Handler>
handler_setup_result setup_response_handler(
    Handler& handler,
    const request& req,
    std::size_t offset,
    context* ctx
)
{
    if (ctx)
    {
        if constexpr (requires { handler.setup(req, offset, ctx); })
            return handler.setup(req, offset, ctx);
    }

    if constexpr (requires { handler.setup(req, offset); })
        return handler.setup(req, offset);
    else
        return handler_setup_result{make_error_code(client_errc::invalid_state)};
}

}  // namespace detail

// Type-erased reference to a response handler
class response_handler_ref
{
    using setup_fn = handler_setup_result (*)(void*, const request&, std::size_t, context*);
    using on_message_fn = void (*)(void*, const any_request_message&, std::size_t, extended_error&);

    void* obj_;
    setup_fn setup_;
    on_message_fn on_message_;

    template <class T>
    static handler_setup_result do_setup(void* obj, const request& req, std::size_t offset, context* ctx)
    {
        return detail::setup_response_handler(*static_cast<T*>(obj), req, offset, ctx);
    }

    template <class T>
    static void do_on_message(
        void* obj,
        const any_request_message& msg,
        std::size_t offset,
        extended_error& err
    )
    {
        static_cast<T*>(obj)->on_message(msg, offset, err);
    }

public:
    template <response_handler T>
    response_handler_ref(T* obj) noexcept : obj_(obj), setup_(&do_setup<T>), on_message_(&do_on_message<T>)
    {
    }

    handler_setup_result setup(const request& req, std::size_t offset, context* ctx = nullptr)
    {
        return setup_(obj_, req, offset, ctx);
    }
    void on_message(const any_request_message& req, std::size_t offset, extended_error& err)
    {
        return on_message_(obj_, req, offset, err);
    }
};

}  // namespace nativepg

#endif
