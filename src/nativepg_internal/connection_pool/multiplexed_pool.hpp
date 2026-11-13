//
// Copyright (c) 2019-2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_MULTIPLEXED_POOL_HPP
#define NATIVEPG_MULTIPLEXED_POOL_HPP

#include <boost/capy/error.hpp>
#include <boost/capy/ex/async_event.hpp>
#include <boost/capy/ex/execution_context.hpp>
#include <boost/capy/ex/this_coro.hpp>
#include <boost/capy/results.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/when_all.hpp>
#include <boost/corosio/delay.hpp>

#include <cstddef>
#include <system_error>

#include "nativepg/co_connection.hpp"
#include "nativepg/co_connection_pool.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/notification_vector.hpp"
#include "nativepg/request.hpp"
#include "nativepg/responses/response_handler.hpp"
#include "nativepg_internal/connection_pool/pool_status.hpp"
#include "nativepg_internal/connection_pool/run_with_timeout.hpp"

namespace nativepg::detail {

class multiplexed_pool_node;

struct multiplexed_pool_shared_state
{
    std::size_t num_healthy{};
    boost::capy::async_event healthy_nodes_ev;
};

class multiplexed_pool_node
{
    std::size_t in_flight_requests_{};
    const pool_params* params_;
    co_connection conn_;
    multiplexed_pool_shared_state* st_;

    // TODO: duplicated
    auto do_connect()
    {
        return run_with_timeout(conn_.connect(params_->transport), params_->connect_timeout);
    }

public:
    multiplexed_pool_node(
        boost::capy::execution_context& ctx,
        const pool_params* params,
        multiplexed_pool_shared_state& st
    )
        : params_(params), conn_(ctx), st_(&st)
    {
    }

    co_connection& connection() { return conn_; }

    std::size_t in_flight_requests() const { return in_flight_requests_; }

    boost::capy::task<boost::capy::results<>> run()
    {
        const auto tok = co_await boost::capy::this_coro::stop_token;
        notification_vector notifs;

        while (!tok.stop_requested())
        {
            // Try to connect
            if (auto [ec] = co_await do_connect(); ec)
            {
                // If we failed, sleep and try again
                static_cast<void>(co_await boost::corosio::delay(params_->retry_interval));
                continue;
            }

            // We've succeeded. Notify tasks that they've got a healthy connection
            ++st_->num_healthy;
            st_->healthy_nodes_ev.set();

            // Now wait until
            //  - It is time to ping (either because of inactivity or because it's been X time since the last
            //  cleanup)
            //  - The connection breaks (we can detect this, in part, using receive())
            //  - There is no completions for Y time, indicating a broken connection
            // TODO: this is a simplistic implementation that takes only point 2 into account
            // TODO: this is a problematic way of detecting errors:
            //  - If a connection encounters an error with receive(), exec()
            //    fails with capy::error::cancelled, without the user cancelling anything.
            //  - We're missing opportunities. If a request hasn't been written or read at all,
            //    it can always be safely retried.
            while (true)
            {
                // If we happen to receive any notifications, discard them
                if (auto [ec] = co_await conn_.receive(notifs); ec)
                    break;
            }

            // We are no longer healthy. TODO: RAII
            if (--st_->num_healthy)
                st_->healthy_nodes_ev.clear();
        }

        co_return {};
    }

    // TODO: I'd prefer if we can avoid some of these intermediate frames
    boost::capy::task<boost::capy::results<>> exec(
        const request& req,
        response_handler_ref handler,
        diagnostics* diag
    )
    {
        // Mark the request in-flight
        ++in_flight_requests_;
        struct in_flight_guard
        {
            multiplexed_pool_node& self;
            in_flight_guard(multiplexed_pool_node& obj) noexcept : self(obj) {}
            in_flight_guard(in_flight_guard&&) = delete;
            in_flight_guard& operator=(in_flight_guard&&) = delete;
            ~in_flight_guard() { --self.in_flight_requests_; }
        } guard{*this};

        // Execute it
        co_return co_await conn_.exec(req, handler, diag);
    }
};

class multiplexed_pool
{
    boost::capy::execution_context& ctx_;
    const pool_params& params_;
    pool_status status_{pool_status::initial};
    std::vector<multiplexed_pool_node> all_nodes_;  // code relies on this not re-allocating
    multiplexed_pool_shared_state shared_st_;
    boost::capy::async_event connections_needed_;

    void create_nodes()
    {
        const auto num_nodes = params_.multiplexed.size;
        BOOST_ASSERT(all_nodes_.empty());
        all_nodes_.reserve(num_nodes);
        for (std::size_t i = 0u; i < num_nodes; ++i)
            all_nodes_.emplace_back(ctx_, &params_, shared_st_);
    }

    std::vector<boost::capy::task<boost::capy::results<>>> make_node_tasks()
    {
        std::vector<boost::capy::task<boost::capy::results<>>> res;
        res.reserve(all_nodes_.size());
        for (auto& n : all_nodes_)
            res.push_back(n.run());
        return res;
    }

    multiplexed_pool_node* find_least_loaded_node()
    {
        multiplexed_pool_node* res = nullptr;
        std::size_t best_load = (std::numeric_limits<std::size_t>::max)();
        for (auto& node : all_nodes_)
        {
            if (node.connection().is_connected() && node.in_flight_requests() < best_load)
            {
                res = &node;
                best_load = node.in_flight_requests();
            }
        }
        return res;
    }

    // Waits until a connection becomes healthy
    boost::capy::task<boost::capy::results<multiplexed_pool_node*>> get_healthy_node()
    {
        while (true)
        {
            // Try to get a healthy node
            if (auto* node = find_least_loaded_node())
                co_return {std::error_code(), node};

            // No luck, wait until one becomes available, or the pool is cancelled
            if (status_ == pool_status::cancelled)
                co_return {boost::capy::error::canceled, nullptr};
            if (auto [ec] = co_await shared_st_.healthy_nodes_ev.wait(); ec)
                co_return {ec, nullptr};
        }
    }

public:
    multiplexed_pool(boost::capy::execution_context& ctx, const pool_params* params)
        : ctx_(ctx), params_(*params)
    {
    }

    boost::capy::task<boost::capy::results<>> run()
    {
        // Check that we're not running and set the state adequately
        // TODO: this is a bit sus. The 'running' check should be done only once
        //   and return already_running. The 'cancelled' status needs to be independent, though.
        BOOST_ASSERT(status_ == pool_status::initial);
        status_ = pool_status::running;

        // Don't create the connections up-front. Wait until at least one request arrives,
        // so we don't pay for what we don't use
        if (auto [ec] = co_await connections_needed_.wait(); ec)
        {
            status_ = pool_status::cancelled;
            co_return {ec};
        }

        // Now go ahead and create the connections. We need to create them using
        // the pool's context because if run() is cancelled, outstanding exec()'s might
        // not have finished by the time run() exits
        create_nodes();

        // Run the nodes until we got cancelled
        BOOST_ASSERT(!all_nodes_.empty());  // guaranteed by param checking
        auto [ec] = co_await boost::capy::when_all(make_node_tasks());

        // Set the pool status, so further requests aren't accepted.
        // Unblock any exec()'s waiting for nodes to become healthy
        // TODO: this probably should be RAII
        status_ = pool_status::cancelled;
        shared_st_.healthy_nodes_ev.set();

        co_return {ec};
    }

    boost::capy::task<boost::capy::results<>> exec(
        const request& req,
        response_handler_ref handler,
        diagnostics* diag = nullptr
    )
    {
        // If the pool is cancelled, the operation must fail, even if there are available connections
        if (status_ == pool_status::cancelled)
            co_return {boost::capy::error::canceled};

        // Notify that we're in need of a connection
        connections_needed_.set();

        // Wait until a connection is healthy.
        auto [ec, node] = co_await get_healthy_node();
        if (ec)
            co_return {ec};

        // Execute the request on the connection we just got
        BOOST_ASSERT(node != nullptr);
        co_return co_await node->connection().exec(req, handler, diag);
    }
};

}  // namespace nativepg::detail

#endif
