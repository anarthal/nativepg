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

#include "nativepg/protocol/connection_state.hpp"
#include "nativepg/protocol/messages.hpp"
#include "nativepg/request.hpp"
#include "nativepg/response.hpp"

namespace nativepg::protocol {

using response_handler_ref = boost::compat::function_ref<
    boost::system::error_code(const any_request_message&)>;

namespace detail {

class read_response_fsm_impl
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

    read_response_fsm_impl(const request& req, response_handler_ref handler) noexcept
        : req_(&req), handler_(handler)
    {
    }

    result resume(const any_backend_message& msg);

private:
    const request* req_;
    response_handler_ref handler_;
    bool handler_finished_{};
    std::size_t remaining_syncs_{};
    bool initial_{true};
};

}  // namespace detail

}  // namespace nativepg::protocol

#endif
