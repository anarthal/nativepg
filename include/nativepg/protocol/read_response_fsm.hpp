//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_PROTOCOL_READ_RESPONSE_FSM_HPP
#define NATIVEPG_PROTOCOL_READ_RESPONSE_FSM_HPP

#include <boost/compat/function_ref.hpp>
#include <boost/system/error_code.hpp>

#include <cstddef>
#include <span>

#include "nativepg/protocol/any_backend_message.hpp"
#include "nativepg/protocol/notice_error.hpp"
#include "nativepg/request.hpp"
#include "nativepg/response_handler.hpp"

namespace nativepg::protocol {

class read_response_fsm
{
public:
    enum class result_type
    {
        done,
        read,
    };

    struct result
    {
        result_type type;
        boost::system::error_code ec;

        result(boost::system::error_code ec) noexcept : type(result_type::done), ec(ec) {}
        result(result_type t) noexcept : type(t) {}
    };

    read_response_fsm(const request* req, response_handler_ref handler, bool allow_copy = false) noexcept
        : req_(req), handler_(handler), allow_copy_(allow_copy)
    {
        BOOST_ASSERT(req != nullptr);
    }

    const request& get_request() const { return *req_; }
    response_handler_ref get_handler() const { return handler_; }

    std::span<const request_message_type> get_remaining_messages() const
    {
        return req_->messages().subspan(current_);
    }

    result resume(const any_backend_message& msg);

private:
    enum class state_t;

    const request* req_;
    response_handler_ref handler_;
    std::size_t current_{};
    state_t state_{static_cast<state_t>(0)};
    bool allow_copy_;

    inline void call_handler(const any_request_message& msg) { handler_.on_message(msg, current_); }
    result advance();
    result handle_bind(const any_backend_message&);
    result handle_close(const any_backend_message&);
    result handle_describe(const any_backend_message&);
    result handle_execute(const any_backend_message&);
    result handle_parse(const any_backend_message&);
    result handle_sync(const any_backend_message&);
    result handle_query(const any_backend_message&);
    result handle_error(const error_response& err);
};

}  // namespace nativepg::protocol

#endif
