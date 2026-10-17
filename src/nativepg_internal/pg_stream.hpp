//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_PG_STREAM_HPP
#define NATIVEPG_PG_STREAM_HPP

#include <boost/assert.hpp>
#include <boost/capy/buffers.hpp>
#include <boost/capy/ex/async_event.hpp>
#include <boost/capy/ex/execution_context.hpp>
#include <boost/capy/ex/executor_ref.hpp>
#include <boost/capy/io_task.hpp>
#include <boost/corosio/connect.hpp>
#include <boost/corosio/resolver.hpp>
#include <boost/corosio/socket_option.hpp>
#include <boost/corosio/tcp_socket.hpp>

#include <cstddef>

#include "nativepg/connect_params.hpp"

namespace nativepg::detail {

class pg_stream
{
    boost::corosio::tcp_socket sock_;
    boost::corosio::resolver resolv_;
    bool write_running_{}, read_running_{};
    boost::capy::async_event all_finished_;

public:
    explicit pg_stream(boost::capy::execution_context& ctx) : sock_(ctx), resolv_(ctx) {}

    // TODO: this is fine but it's overhead per-stream-op.
    // Optimizable? I.e. leaf awaitable?
    template <boost::capy::ConstBufferSequence Buffers>
    boost::capy::io_task<std::size_t> write_some(Buffers&& buffs)
    {
        BOOST_ASSERT(!write_running_);
        write_running_ = true;  // TODO: we could use some RAII
        all_finished_.clear();
        auto res = co_await sock_.write_some(std::forward<Buffers>(buffs));
        write_running_ = false;
        if (!read_running_)
            all_finished_.set();
        co_return res;
    }

    template <boost::capy::MutableBufferSequence Buffers>
    boost::capy::io_task<std::size_t> read_some(Buffers&& buffs)
    {
        BOOST_ASSERT(!read_running_);
        read_running_ = true;  // TODO: we could use some RAII
        all_finished_.clear();
        auto res = co_await sock_.read_some(std::forward<Buffers>(buffs));
        read_running_ = false;
        if (!write_running_)
            all_finished_.set();
        co_return res;
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

    // Waits until all pending reads and writes complete
    auto join() { return all_finished_.wait(); }
};

}  // namespace nativepg::detail

#endif
