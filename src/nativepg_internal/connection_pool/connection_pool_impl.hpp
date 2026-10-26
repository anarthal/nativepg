//
// Copyright (c) 2019-2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_CONNECTION_POOL_IMPL_HPP
#define NATIVEPG_CONNECTION_POOL_IMPL_HPP

#include <boost/assert.hpp>
#include <boost/capy/cond.hpp>
#include <boost/capy/error.hpp>
#include <boost/capy/ex/async_event.hpp>
#include <boost/capy/ex/await_context.hpp>
#include <boost/capy/ex/execution_context.hpp>
#include <boost/capy/ex/executor_ref.hpp>
#include <boost/capy/ex/run.hpp>
#include <boost/capy/ex/run_async.hpp>
#include <boost/capy/ex/this_coro.hpp>
#include <boost/capy/results.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/when_any.hpp>

#include <algorithm>
#include <cstddef>
#include <list>
#include <stop_token>

#include "nativepg/co_connection_pool.hpp"
#include "nativepg_internal/connection_pool/check_pool_params.hpp"
#include "nativepg_internal/connection_pool/pool_node.hpp"
#include "nativepg_internal/op_counter.hpp"

namespace nativepg::detail {

// Given config params and the current state, computes the number
// of connections that the pool should create at any given point in time
inline std::size_t num_connections_to_create(
    std::size_t initial_size,         // config
    std::size_t max_size,             // config
    std::size_t current_connections,  // the number of connections in the pool, in any state
    std::size_t in_use_connections,   // the number of connections in the pool handed to the user
    std::size_t pending_requests      // the current number of async_get_connection requests that are waiting
)
{
    BOOST_ASSERT(initial_size <= max_size);
    BOOST_ASSERT(current_connections <= max_size);
    BOOST_ASSERT(in_use_connections <= current_connections);

    // Number of connections that are getting ready (in the process of being connected, reset, ...)
    const std::size_t pending_connections = current_connections - in_use_connections;

    // We aim to have one pending connection per pending request.
    // When these connections successfully connect, they will fulfill the pending requests.
    std::size_t required_by_requests = pending_requests > pending_connections
                                           ? static_cast<std::size_t>(pending_requests - pending_connections)
                                           : 0u;

    // We should always have at least min_connections.
    // This might not be the case if the pool is just starting.
    std::size_t required_by_min = current_connections < initial_size
                                      ? static_cast<std::size_t>(initial_size - current_connections)
                                      : 0u;

    // We can't excess max_connections. This is the room for new connections that we have
    std::size_t room = static_cast<std::size_t>(max_size - current_connections);

    return (std::min)((std::max)(required_by_requests, required_by_min), room);
}

class co_connection_pool_impl
{
    enum class state_t
    {
        initial,
        running,
        cancelled,
    };

    pool_params params_;
    state_t state_{state_t::initial};
    std::list<pool_node> all_conns_;
    pool_shared_state shared_st_;
    boost::capy::async_event cancel_ev_;
    boost::capy::async_event connections_needed_;
    op_counter running_connections_counter_;

    // The number of async_get_connection ops that are waiting for a connection to become available.
    // Required to compute how many connections we should create at any given point in time.
    std::size_t num_pending_requests_{0};

    boost::capy::task<> run_one(pool_node& node)
    {
        auto guard = running_connections_counter_.scoped_notify_start();
        static_cast<void>(co_await boost::capy::when_any(node.run(), cancel_ev_.wait()));
    }

    // Create and run one connection
    // This should be safe as long as we ensure that the parent task (pool run)
    // doesn't finish until all connection tasks terminate.
    // We don't propagate the stop_token because ordering of cleanup callbacks is important
    void create_connection(const boost::capy::await_context* env)
    {
        all_conns_.emplace_back(env->executor.context(), &params_, shared_st_);
        boost::capy::run_async(env->executor, env->frame_allocator)(run_one(all_conns_.back()));
    }

    // Create and run connections as required by the current config and state
    void create_connections(const boost::capy::await_context* env)
    {
        // Calculate how many we should create
        std::size_t n = num_connections_to_create(
            params_.initial_size,
            params_.max_size,
            all_conns_.size(),
            shared_st_.num_in_use_connections,
            num_pending_requests_
        );

        // Create them
        BOOST_ASSERT((all_conns_.size() + n) <= params_.max_size);
        for (std::size_t i = 0; i < n; ++i)
            create_connection(env);
    }

    // An async_get_connection request is about to wait for an available connection
    void enter_request_pending()
    {
        // Record that we're pending
        ++num_pending_requests_;

        // Tell run() that we need connections
        connections_needed_.set();
    }

    // An async_get_connection request finished waiting
    void exit_request_pending()
    {
        // Record that we're no longer pending
        BOOST_ASSERT(num_pending_requests_ > 0u);
        --num_pending_requests_;
    }

    pool_node* try_get_connection()
    {
        if (!shared_st_.idle_list.empty())
        {
            auto& res = shared_st_.idle_list.front();
            res.mark_as_in_use();
            return &res;
        }
        else
        {
            return nullptr;
        }
    }

public:
    co_connection_pool_impl(boost::capy::execution_context&, pool_params&& params)
        : params_(std::move(params))
    {
        check_pool_params(params_);
    }

    boost::capy::task<boost::capy::results<>> run()
    {
        // Check that we're not running and set the state adequately
        BOOST_ASSERT(state_ == state_t::initial);
        state_ = state_t::running;

        const auto* env = co_await boost::capy::this_coro::environment;

        // Create the initial connections
        create_connections(env);

        while (true)
        {
            // Wait until more connections are needed. Exit on cancellation
            if (auto [ec] = co_await connections_needed_.wait(); ec)
            {
                BOOST_ASSERT(ec == boost::capy::cond::canceled);
                break;
            }

            // Acknowledge the notification
            connections_needed_.clear();

            // Create the required connections
            create_connections(env);
        }

        // Set the state so further get_connection requests fail
        state_ = state_t::cancelled;

        // Deliver the cancel notification to the child tasks
        cancel_ev_.set();

        // Unblock any further waits in get_connection
        shared_st_.idle_connections_cv.notify_all();

        // Wait for all connection tasks to exit. We need to replace the stop token so this has any effect.
        [[maybe_unused]] auto [ec2] = co_await boost::capy::run(std::stop_token())(
            running_connections_counter_.wait()
        );
        BOOST_ASSERT(!ec2);

        // Done
        co_return {boost::capy::error::canceled};
    }

    boost::capy::task<boost::capy::results<pooled_connection>> get_connection()
    {
        while (true)
        {
            // If the pool is cancelled, the operation must fail, even if there are available connections
            if (state_ == state_t::cancelled)
                co_return {boost::capy::error::canceled, pooled_connection{}};

            // Try to get a connection
            if (auto* node = try_get_connection())
                co_return {std::error_code(), pooled_connection(*node)};

            // No luck. Record that we're waiting for a connection.
            enter_request_pending();

            // Wait to be notified, or until a cancellation happens.
            // Cancellation yields an error here.
            // The loop exists because async_condition posts when notifying, so
            //   - Other tasks might 'steal' the connection that caused the notify while resume
            //   - The pool might shutdown while we resume
            //   - A shutdown notifies us, but doesn't cause an error.
            auto [ec] = co_await shared_st_.idle_connections_cv.wait();

            // Record that we're no longer pending
            exit_request_pending();

            if (ec)
                co_return {ec, pooled_connection()};
        }
    }
};

}  // namespace nativepg::detail

#endif
