//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_PROTOCOL_READ_RESPONSE_FSM_HPP
#define NATIVEPG_PROTOCOL_READ_RESPONSE_FSM_HPP

#include <cstddef>
#include <span>
#include <system_error>

#include "nativepg/extended_error.hpp"
#include "nativepg/protocol/any_backend_message.hpp"
#include "nativepg/request.hpp"
#include "nativepg/responses/response_handler.hpp"

namespace nativepg::protocol {

namespace detail {

// Split here so we don't need to declare private functions in this header
struct read_response_fsm_impl
{
    enum class state_t;

    // Params
    const request* req;
    response_handler_ref handler;
    bool allow_copy;

    // Working state
    std::size_t current{};
    state_t state{static_cast<state_t>(0)};
    extended_error handler_err, dummy_err;
};

}  // namespace detail

class read_response_fsm
{
public:
    read_response_fsm(const request* req, response_handler_ref handler, bool allow_copy = false) noexcept
        : impl_{req, handler, allow_copy}
    {
        BOOST_ASSERT(req != nullptr);
    }

    const request& get_request() const { return *impl_.req; }
    response_handler_ref get_handler() const { return impl_.handler; }
    const extended_error& get_handler_error() const { return impl_.handler_err; }

    std::span<const request_message_type> get_remaining_messages() const
    {
        return impl_.req->messages().subspan(impl_.current);
    }

    // Feeds a message to the FSM. Returns client_errc::needs_more if more messages
    // are required to complete the response, a success code if the response is
    // complete, or any other error code on failure
    std::error_code resume(const any_backend_message& msg);

private:
    detail::read_response_fsm_impl impl_;
};

}  // namespace nativepg::protocol

#endif
