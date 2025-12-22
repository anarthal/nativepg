//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_CONNECTION_HPP
#define NATIVEPG_CONNECTION_HPP

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/async_result.hpp>
#include <boost/asio/compose.hpp>
#include <boost/asio/deferred.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/write.hpp>
#include <boost/system/error_code.hpp>

#include <cstddef>
#include <memory>

#include "nativepg/connect_params.hpp"
#include "nativepg/protocol/connection_state.hpp"
#include "nativepg/protocol/detail/exec_fsm.hpp"
#include "nativepg/protocol/read_response_fsm.hpp"
#include "nativepg/protocol/startup_fsm.hpp"
#include "nativepg/request.hpp"

namespace nativepg {

namespace detail {

struct connection_impl
{
    boost::asio::ip::tcp::socket sock;
    protocol::connection_state st{};

    explicit connection_impl(boost::asio::any_io_executor ex) : sock(std::move(ex)) {}
};

struct connect_op
{
    connection_impl& impl;
    protocol::startup_fsm fsm_;

    template <class Self>
    void operator()(Self& self, boost::system::error_code ec = {}, std::size_t bytes_transferred = {})
    {
        auto res = fsm_.resume(impl.st, ec, bytes_transferred);
        switch (res.type())
        {
            case protocol::startup_fsm::result_type::write:
                boost::asio::async_write(impl.sock, res.write_data(), std::move(self));
                break;
            case protocol::startup_fsm::result_type::read:
                impl.sock.async_read_some(res.read_buffer(), std::move(self));
                break;
            case protocol::startup_fsm::result_type::done: self.complete(res.error()); break;
            default: BOOST_ASSERT(false);
        }
    }
};

struct exec_op
{
    connection_impl& impl;
    protocol::detail::exec_fsm fsm_;

    template <class Self>
    void operator()(Self& self, boost::system::error_code ec = {}, std::size_t bytes_transferred = {})
    {
        auto res = fsm_.resume(impl.st, ec, bytes_transferred);
        switch (res.type())
        {
            case protocol::startup_fsm::result_type::write:
                boost::asio::async_write(impl.sock, res.write_data(), std::move(self));
                break;
            case protocol::startup_fsm::result_type::read:
                impl.sock.async_read_some(res.read_buffer(), std::move(self));
                break;
            case protocol::startup_fsm::result_type::done: self.complete(res.error()); break;
            default: BOOST_ASSERT(false);
        }
    }
};

}  // namespace detail

class connection
{
    std::unique_ptr<detail::connection_impl> impl_;

public:
    explicit connection(boost::asio::any_io_executor ex) : impl_(new detail::connection_impl{std::move(ex)})
    {
    }
    // TODO: ctor from execution context

    boost::asio::any_io_executor get_executor() { return impl_->sock.get_executor(); }

    // TODO: we shouldn't need this
    boost::asio::ip::tcp::socket& stream() { return impl_->sock; }

    template <
        boost::asio::completion_token_for<void(boost::system::error_code)> CompletionToken =
            boost::asio::deferred_t>
    auto async_connect(const connect_params& params, CompletionToken&& token = {})
    {
        return boost::asio::async_compose<CompletionToken, void(boost::system::error_code)>(
            detail::connect_op{*impl_, protocol::startup_fsm{params}},
            token,
            impl_->sock
        );
    }

    template <
        boost::asio::completion_token_for<void(boost::system::error_code)> CompletionToken =
            boost::asio::deferred_t>
    auto async_exec(const request& req, protocol::response_handler_ref handler, CompletionToken&& token = {})
    {
        return boost::asio::async_compose<CompletionToken, void(boost::system::error_code)>(
            detail::exec_op{
                *impl_,
                protocol::detail::exec_fsm{req, handler}
        },
            token,
            impl_->sock
        );
    }
};

}  // namespace nativepg

#endif
