//
// Copyright (c) 2019-2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_MULTIPLEXED_POOL_HPP
#define NATIVEPG_MULTIPLEXED_POOL_HPP

#include <boost/assert.hpp>
#include <boost/capy/error.hpp>
#include <boost/capy/ex/execution_context.hpp>
#include <boost/capy/results.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/when_all.hpp>

#include <cstddef>
#include <system_error>
#include <vector>

#include "nativepg/co_connection_pool.hpp"
#include "nativepg_internal/connection_pool/pool_node.hpp"
#include "nativepg_internal/connection_pool/pool_status.hpp"

namespace nativepg::detail {

class multiplexed_pool
{
    boost::capy::execution_context& ctx_;
    const pool_params& params_;
    pool_status status_{pool_status::initial};
    std::vector<multiplexed_pool_node> all_nodes_;
    multiplexed_shared_state shared_st_;
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

    // Waits until a connection becomes healthy
    boost::capy::task<boost::capy::results<multiplexed_pool_node*>> get_healthy_node()
    {
        while (true)
        {
            // Try to get a healthy node
            if (!shared_st_.healthy_nodes.empty())
                co_return {std::error_code(), &shared_st_.healthy_nodes.front()};

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
        auto [ec] = co_await boost::capy::when_all(make_node_tasks());

        // Set the pool status, so further requests aren't accepted.
        // Unblock any exec()'s waiting for nodes to become healthy
        // TODO: this probably should be RAII
        status_ = pool_status::cancelled;
        shared_st_.healthy_nodes_ev.set();

        // TODO: what happens if the number of connections is 0?

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
