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

    struct visitor;
};

}  // namespace detail

class read_response_fsm
{
public:
    enum class result_type
    {
        done,
        read,
    };

    class result
    {
        result_type type_;
        union
        {
            boost::system::error_code ec_;
            std::span<unsigned char> read_buff_;
        };

        result(result_type t, std::span<unsigned char> data) noexcept : type_(t), read_buff_(data) {}

    public:
        result(boost::system::error_code ec) noexcept : type_(result_type::done), ec_(ec) {}

        static result read(std::span<unsigned char> buff) { return {result_type::read, buff}; }

        result_type type() const { return type_; }

        boost::system::error_code error() const
        {
            BOOST_ASSERT(type_ == result_type::done);
            return ec_;
        }

        std::span<unsigned char> read_buffer() const
        {
            BOOST_ASSERT(type_ == result_type::read);
            return read_buff_;
        }
    };

    read_response_fsm(const request& req, response_handler_ref handler) noexcept : impl_(req, handler) {}

    result resume(connection_state& st, boost::system::error_code io_error, std::size_t bytes_read);

private:
    detail::read_response_fsm_impl impl_;
};

}  // namespace nativepg::protocol

#endif
