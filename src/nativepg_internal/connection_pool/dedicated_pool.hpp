//
// Copyright (c) 2019-2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_DEDICATED_POOL_HPP
#define NATIVEPG_DEDICATED_POOL_HPP

#include <boost/assert.hpp>
#include <boost/capy/cond.hpp>
#include <boost/capy/error.hpp>
#include <boost/capy/ex/async_event.hpp>
#include <boost/capy/ex/async_waker.hpp>
#include <boost/capy/ex/await_context.hpp>
#include <boost/capy/ex/execution_context.hpp>
#include <boost/capy/ex/executor_ref.hpp>
#include <boost/capy/ex/run.hpp>
#include <boost/capy/ex/run_async.hpp>
#include <boost/capy/ex/this_coro.hpp>
#include <boost/capy/results.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/when_any.hpp>
#include <boost/corosio/delay.hpp>
#include <boost/intrusive/list.hpp>
#include <boost/intrusive/list_hook.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <list>
#include <stop_token>
#include <system_error>

#include "nativepg/capy-stub/ex/async_condition.hpp"
#include "nativepg/co_connection.hpp"
#include "nativepg/co_connection_pool.hpp"
#include "nativepg/protocol/ready_for_query.hpp"
#include "nativepg/request.hpp"
#include "nativepg/responses/check.hpp"
#include "nativepg_internal/connection_pool/pool_status.hpp"
#include "nativepg_internal/connection_pool/run_with_timeout.hpp"
#include "nativepg_internal/op_counter.hpp"

namespace nativepg::detail {

// Given config params and the current state, computes the number
// of connections that the pool should create at any given point in time
inline std::size_t num_connections_to_create(
    const dedicated_pool_params& params,
    std::size_t current_connections,  // the number of connections in the pool, in any state
    std::size_t in_use_connections,   // the number of connections in the pool handed to the user
    std::size_t pending_requests      // the current number of async_get_connection requests that are waiting
)
{
    BOOST_ASSERT(params.initial_size <= params.max_size);
    BOOST_ASSERT(current_connections <= params.max_size);
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
    std::size_t required_by_min = current_connections < params.initial_size
                                      ? static_cast<std::size_t>(params.initial_size - current_connections)
                                      : 0u;

    // We can't excess max_connections. This is the room for new connections that we have
    std::size_t room = static_cast<std::size_t>(params.max_size - current_connections);

    return (std::min)((std::max)(required_by_requests, required_by_min), room);
}

class dedicated_pool_node;

// State shared between pool tasks
// TODO: logging
struct dedicated_pool_shared_state
{
    // The list of connections that are currently idle. Non-owning.
    // Using base_hook explicitly is required because pool_node is incomplete
    boost::intrusive::
        list<dedicated_pool_node, boost::intrusive::base_hook<boost::intrusive::list_base_hook<>>>
            idle_list;

    // Condition variable to wait for idle connections
    boost::capy::async_condition idle_connections_cv;

    // The number of connection that are currently in-use (have been handed by get_connection)
    // Required to compute how many connections we should create at any given point in time.
    std::size_t num_in_use_connections{0u};
};

// TODO: recover thread-safety tests
class dedicated_pool_node : public boost::intrusive::list_base_hook<>
{
    const pool_params* params_;
    request ping_req_;
    dedicated_pool_shared_state* shared_st_;
    co_connection conn_;
    check check_handler;

    // A collection_state represents the possibility that a connection
    // that was in use was returned by the user
    enum class collection_state
    {
        // Connection wasn't returned
        none,

        // Connection was returned and doesn't need reset
        needs_collect,

        // Connection was returned and needs reset
        needs_collect_with_reset
    };

    // What should run() do after idle_wait() finishes?
    enum class collection_action
    {
        // Run a ping
        ping,

        // Mark the connection as returned, no reset needed
        collect,

        // Mark the connection as returned, reset needed
        collect_with_reset,
    };

    // Thread-safe, for collection notifications.
    boost::capy::async_waker collection_waker_;
    std::atomic<collection_state> collection_state_{collection_state::none};

    boost::capy::task<boost::capy::results<collection_action>> idle_wait()
    {
        while (true)
        {
            auto [ec] = co_await run_with_timeout(collection_waker_.wait(), params_->ping_interval);
            if (ec == boost::capy::cond::timeout)
            {
                if (is_idle())
                {
                    // The user hasn't picked the connection and the ping interval has elapsed.
                    // Time to ping
                    co_return {std::error_code(), collection_action::ping};
                }

                // The user is still using the connection. Go back to waiting
            }
            else if (ec)
            {
                // Likely a cancellation
                co_return {ec, collection_action::ping};
            }
            else
            {
                switch (collection_state_.exchange(collection_state::none))
                {
                    case collection_state::needs_collect:
                        co_return {std::error_code(), collection_action::collect};
                    case collection_state::needs_collect_with_reset:
                        co_return {std::error_code(), collection_action::collect_with_reset};
                    case collection_state::none: break;
                    default: BOOST_ASSERT(false); break;
                }
            }
        }
    }

    void entering_idle()
    {
        BOOST_ASSERT(!is_idle());
        shared_st_->idle_list.push_back(*this);
        shared_st_->idle_connections_cv.notify_one();
    }
    void exiting_idle()
    {
        BOOST_ASSERT(is_idle());
        shared_st_->idle_list.erase(shared_st_->idle_list.iterator_to(*this));
    }
    void entering_in_use() { ++shared_st_->num_in_use_connections; }
    void exiting_in_use() { --shared_st_->num_in_use_connections; }

    bool is_idle() const { return is_linked(); }

    auto do_connect()
    {
        return run_with_timeout(conn_.connect(params_->transport), params_->connect_timeout);
    }

    auto do_ping() { return run_with_timeout(conn_.exec(ping_req_, &check_handler), params_->ping_timeout); }

    // TODO: do we need another timeout?
    auto do_reset() { return run_with_timeout(reset_connection(), params_->ping_timeout); }

    boost::capy::task<boost::capy::results<>> reset_connection()
    {
        // Compose the reset request.
        // DISCARD ALL can't be used in a transaction block, so the rollback must be before it.
        // ROLLBACK without an open transaction block issues a notice
        request reset_req;
        if (conn_.txn_status() != protocol::transaction_status::idle)
            reset_req.add_simple_query("ROLLBACK");
        reset_req.add_simple_query("DISCARD ALL");

        co_return co_await conn_.exec(reset_req, &check_handler);
    }

public:
    dedicated_pool_node(
        boost::capy::execution_context& ctx,
        const pool_params* params,
        dedicated_pool_shared_state& shared_st
    )
        : params_(params), shared_st_(&shared_st), conn_(ctx)
    {
        // There is no explicit PING command, but sending a sync will cause
        // the server to answer with ready_for_query
        ping_req_.add_sync();
    }

    void mark_as_in_use() noexcept
    {
        exiting_idle();
        entering_in_use();
    }

    // Thread-safe
    void mark_as_collectable(bool should_reset)
    {
        collection_state_.store(
            should_reset ? collection_state::needs_collect_with_reset : collection_state::needs_collect
        );
        collection_waker_.wake();
    }

    boost::capy::task<boost::capy::results<>> run()
    {
        const auto tok = co_await boost::capy::this_coro::stop_token;

        // If we exit and we're in the idle list, remove ourselves from it.
        // Otherwise, safe_link mode may trigger an assert on pool destruction.
        struct idle_guard_t
        {
            dedicated_pool_node& self;
            idle_guard_t(dedicated_pool_node& self) noexcept : self(self) {}
            ~idle_guard_t()
            {
                if (self.is_idle())
                    self.exiting_idle();
            }
        } idle_guard{*this};

        while (!tok.stop_requested())
        {
            // Try to connect
            if (auto [ec] = co_await do_connect(); ec)
            {
                // If we failed, sleep and try again
                static_cast<void>(co_await boost::corosio::delay(params_->retry_interval));
                continue;
            }

            // We've succeeded and are now idle
            entering_idle();

            while (true)
            {
                // Wait until either it's time to ping, or we're notified for a collection.
                // Note that the idle => in-use transition is performed outside this coro in mark_as_in_use()
                auto [ec, act] = co_await idle_wait();
                if (ec)
                    co_return {};  // cancellation

                if (act == collection_action::ping)
                {
                    // We shouldn't hand this connection to the user until the ping finishes
                    exiting_idle();
                    if (auto [ec] = co_await do_ping(); ec)
                    {
                        // Ping failed. Reconnect
                        break;
                    }

                    // Ping succeeded. We're idle again
                    entering_idle();
                }
                else
                {
                    // We're no longer in-use
                    exiting_in_use();

                    // If the connection is marked as broken, reconnect.
                    // TODO: leftovers could be handled without reconnection.
                    //   This is simpler though.
                    if (!conn_.is_connected() || conn_.has_leftovers())
                        break;

                    // We should reset if the user requested it, or if we
                    // detect an open transaction. txn_status() is reliable
                    // here because we have no leftovers to read.
                    if (act == collection_action::collect_with_reset ||
                        conn_.txn_status() != protocol::transaction_status::idle)
                    {
                        if (auto [ec] = co_await do_reset(); ec)
                            break;  // reset failed, reconnect
                    }

                    // Discard any unread notifications generated during the session.
                    // Must happen without leftovers and after any potential reset,
                    // as notifications might be generated until the reset becomes effective.
                    conn_.clear_cached_notifications();

                    // We have successfully cleaned up the connection. We're idle again
                    entering_idle();
                }
            }
        }

        co_return {};
    }

    // Getters, used by pooled_connection
    co_connection& connection() noexcept { return conn_; }
};

class dedicated_pool
{
    boost::capy::execution_context& ctx_;
    const pool_params& params_;
    pool_status status_{pool_status::initial};
    std::list<dedicated_pool_node> all_conns_;
    dedicated_pool_shared_state shared_st_;
    boost::capy::async_event cancel_ev_;
    boost::capy::async_event connections_needed_;
    op_counter running_connections_counter_;

    // The number of async_get_connection ops that are waiting for a connection to become available.
    // Required to compute how many connections we should create at any given point in time.
    std::size_t num_pending_requests_{0};

    boost::capy::task<> run_one(dedicated_pool_node& node, op_counter::guard)
    {
        static_cast<void>(co_await boost::capy::when_any(node.run(), cancel_ev_.wait()));
    }

    // Create and run one connection
    // This should be safe as long as we ensure that the parent task (pool run)
    // doesn't finish until all connection tasks terminate.
    // We don't propagate the stop_token because ordering of cleanup callbacks is important
    void create_connection(const boost::capy::await_context* env)
    {
        // Create the connection
        all_conns_.emplace_back(ctx_, &params_, shared_st_);

        // Record that we have a task running. Doing this here instead of in run_one()
        // prevents potential race conditions (op_counter::wait() running before the start notification)
        auto guard = running_connections_counter_.scoped_notify_start();

        // Actually start the task
        boost::capy::run_async(env->executor, env->frame_allocator)(
            run_one(all_conns_.back(), std::move(guard))
        );
    }

    // Create and run connections as required by the current config and state
    void create_connections(const boost::capy::await_context* env)
    {
        // Calculate how many we should create
        std::size_t n = num_connections_to_create(
            params_.dedicated,
            all_conns_.size(),
            shared_st_.num_in_use_connections,
            num_pending_requests_
        );

        // Create them
        BOOST_ASSERT((all_conns_.size() + n) <= params_.dedicated.max_size);
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

    dedicated_pool_node* try_get_connection()
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
    dedicated_pool(boost::capy::execution_context& ctx, const pool_params* params)
        : ctx_(ctx), params_(*params)
    {
    }

    boost::capy::task<boost::capy::results<>> run()
    {
        // Check that we're not running and set the state adequately
        BOOST_ASSERT(status_ == pool_status::initial);
        status_ = pool_status::running;

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
        status_ = pool_status::cancelled;

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
            if (status_ == pool_status::cancelled)
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
