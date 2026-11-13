//
// Copyright (c) 2019-2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_POOL_NODE_HPP
#define NATIVEPG_POOL_NODE_HPP

#include <boost/assert.hpp>
#include <boost/capy/cond.hpp>
#include <boost/capy/ex/async_event.hpp>
#include <boost/capy/ex/async_waker.hpp>
#include <boost/capy/ex/execution_context.hpp>
#include <boost/capy/ex/this_coro.hpp>
#include <boost/capy/results.hpp>
#include <boost/capy/task.hpp>
#include <boost/corosio/delay.hpp>
#include <boost/intrusive/list.hpp>
#include <boost/intrusive/list_hook.hpp>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <system_error>

#include "nativepg/capy-stub/ex/async_condition.hpp"
#include "nativepg/co_connection.hpp"
#include "nativepg/co_connection_pool.hpp"
#include "nativepg/notification_vector.hpp"
#include "nativepg/protocol/ready_for_query.hpp"
#include "nativepg/request.hpp"
#include "nativepg/responses/check.hpp"
#include "nativepg_internal/connection_pool/run_with_timeout.hpp"

namespace nativepg::detail {

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

// TODO: move
class multiplexed_pool_node;

struct multiplexed_pool_shared_state
{
    // TODO: sort this by busy-ness
    boost::intrusive::
        list<multiplexed_pool_node, boost::intrusive::base_hook<boost::intrusive::list_base_hook<>>>
            healthy_nodes;
    boost::capy::async_event healthy_nodes_ev;
};

class multiplexed_pool_node : public boost::intrusive::list_base_hook<>
{
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
            st_->healthy_nodes.push_back(*this);
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
            st_->healthy_nodes.erase(st_->healthy_nodes.iterator_to(*this));
            if (st_->healthy_nodes.empty())
                st_->healthy_nodes_ev.clear();
        }

        co_return {};
    }
};

}  // namespace nativepg::detail

#endif
