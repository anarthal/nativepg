//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_PG_STREAM_HPP
#define NATIVEPG_PG_STREAM_HPP

#include <boost/capy/buffers.hpp>
#include <boost/capy/ex/execution_context.hpp>
#include <boost/capy/io_task.hpp>
#include <boost/corosio/connect.hpp>
#include <boost/corosio/resolver.hpp>
#include <boost/corosio/socket_option.hpp>
#include <boost/corosio/tcp_socket.hpp>

#include <string>
#include <utility>

#include "nativepg/connect_params.hpp"

namespace nativepg::detail {

class pg_stream
{
    boost::corosio::tcp_socket sock_;
    boost::corosio::resolver resolv_;

public:
    explicit pg_stream(boost::capy::execution_context& ctx) : sock_(ctx), resolv_(ctx) {}

    template <boost::capy::ConstBufferSequence Buffers>
    auto write_some(Buffers&& buffs)
    {
        return sock_.write_some(std::forward<Buffers>(buffs));
    }

    template <boost::capy::MutableBufferSequence Buffers>
    auto read_some(Buffers&& buffs)
    {
        return sock_.read_some(std::forward<Buffers>(buffs));
    }

    boost::capy::io_task<> connect(const connect_params& params)
    {
        auto [ec, endpoints] = co_await resolv_.resolve(params.hostname, std::to_string(params.port));
        if (ec)
            co_return {ec};

        auto [ec2, ep] = co_await boost::corosio::connect(sock_, endpoints);
        if (ec2)
            co_return {ec2};

        // Disable Nagle's algorithm.
        // Must be done after async_connect because it re-opens the socket
        // for every candidate endpoint, discarding options.
        sock_.set_option(boost::corosio::socket_option::no_delay(true));

        co_return {};
    }

    void close()
    {
        // No tcp_socket::shutdown() here to match what libpq does.
        // At least on Linux, it does nothing:
        // both close() and shutdown(SHUT_RDWR) will send a RST if there is pending
        // data in the read buffer (e.g. a pending NotificationResponse),
        // and a FIN otherwise. Should't be a big deal.
        sock_.close();
    }
};

}  // namespace nativepg::detail

#endif
