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

    // Working state
    std::size_t current{};
    state_t state{static_cast<state_t>(0)};
    extended_error handler_err{};
};

}  // namespace detail

class read_response_fsm
{
public:
    read_response_fsm(const request* req, response_handler_ref handler) noexcept : impl_{req, handler}
    {
        BOOST_ASSERT(req != nullptr);
    }

    // TODO: I don't like this. It's somehow specific to how _we_ handle Copy-in.
    // There could be other strategies.
    struct result_type
    {
        std::error_code ec;       // special: needs_more, copy_in
        std::size_t num_syncs{};  // if copy_in, number of syncs that will be swallowed by the server

        friend bool operator==(const result_type&, const result_type&) = default;
    };

    const request& get_request() const { return *impl_.req; }
    response_handler_ref get_handler() const { return impl_.handler; }
    const extended_error& get_handler_error() const { return impl_.handler_err; }

    std::span<const request_message_type> get_remaining_messages() const
    {
        return impl_.req->messages().subspan(impl_.current);
    }

    // Feeds a message to the FSM. The returned ec is:
    //   - client_errc::needs_more if more messages are required to complete the response.
    //   - A success code if the response is complete.
    //   - client_errc::copy_in if the server accepted a COPY ... FROM STDIN and is now
    //     expecting copy data from us. The client must drive the copy_data/copy_done/copy_fail
    //     flow and then keep calling resume() to read the rest of the response.
    //     result_type::num_syncs then holds the number of Sync messages that the server
    //     will discard while in copy-in mode (zero for the simple query protocol), and that
    //     must be re-sent after CopyDone/CopyFail for the response to complete.
    //   - Any other error code on failure.
    result_type resume(const any_backend_message& msg);

private:
    detail::read_response_fsm_impl impl_;
};

}  // namespace nativepg::protocol

#endif
