//
// Copyright (c) 2019-2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_POOL_NODE_HPP
#define NATIVEPG_POOL_NODE_HPP

#include <boost/capy/cond.hpp>
#include <boost/capy/ex/async_event.hpp>
#include <boost/capy/ex/execution_context.hpp>
#include <boost/capy/ex/this_coro.hpp>
#include <boost/capy/results.hpp>
#include <boost/capy/task.hpp>
#include <boost/corosio/delay.hpp>
#include <boost/corosio/timeout.hpp>
#include <boost/intrusive/list.hpp>
#include <boost/intrusive/list_hook.hpp>

#include <chrono>
#include <system_error>
#include <utility>

#include "nativepg/capy-stub/ex/async_condition.hpp"
#include "nativepg/co_connection.hpp"
#include "nativepg/co_connection_pool.hpp"
#include "nativepg/protocol/sync.hpp"
#include "nativepg/request.hpp"
#include "nativepg/responses/check.hpp"
#include "nativepg_internal/op_counter.hpp"

namespace nativepg::detail {

// A collection_state represents the possibility that a connection
// that was in_use was returned by the user
enum class collection_state
{
    // Connection wasn't returned
    none,

    // Connection was returned and needs reset
    needs_collect,

    // Connection was returned and doesn't need reset
    needs_collect_with_reset
};

// State shared between pool tasks
// TODO: logging
template <class Node>
struct pool_shared_state
{
    // The list of connections that are currently idle. Non-owning.
    boost::intrusive::list<Node> idle_list;

    // Condition variable to wait for idle connections
    boost::capy::async_condition idle_connections_cv;

    // The number of pending connections (currently getting ready).
    // Required to compute how many connections we should create at any given point in time.
    std::size_t num_pending_connections{0};

    // The number of async_get_connection ops that are waiting for a connection to become available.
    // Required to compute how many connections we should create at any given point in time.
    std::size_t num_pending_requests{0};

    // Tracks running connections, so run() can wait for them
    op_counter running_connections_counter;
};

template <class Awaitable>
boost::capy::task<boost::capy::results<>> run_with_timeout(
    Awaitable aw,
    std::chrono::steady_clock::duration dur
)
{
    if (dur.count() > 0)
    {
        co_return co_await boost::corosio::timeout(std::move(aw), dur);
    }
    else
    {
        co_return co_await std::move(aw);
    }
}

class pool_node : public boost::intrusive::list_base_hook<>
{
    const pool_params* params_;
    request ping_req_;
    pool_shared_state<pool_node>* shared_st_;
    co_connection conn_;
    boost::capy::async_event collection_ev_;  // Notifications about collections
    collection_state collection_state_{collection_state::none};
    check check_handler;

    void entering_idle()
    {
        shared_st_->idle_list.push_back(*this);
        shared_st_->idle_connections_cv.notify_one();
    }
    void exiting_idle() { shared_st_->idle_list.erase(shared_st_->idle_list.iterator_to(*this)); }
    void entering_pending() { ++shared_st_->num_pending_connections; }
    void exiting_pending() { --shared_st_->num_pending_connections; }

    bool is_idle() const { return is_linked(); }

    boost::capy::task<boost::capy::results<>> do_shutdown()
    {
        // TODO: do we need another timeout value here?
        return run_with_timeout(conn_.shutdown(), params_->ping_timeout);
    }

    boost::capy::task<boost::capy::results<>> do_connect()
    {
        return run_with_timeout(conn_.connect(params_->transport), params_->connect_timeout);
    }

    boost::capy::task<boost::capy::results<>> do_ping()
    {
        return run_with_timeout(conn_.exec(ping_req_, &check_handler), params_->ping_timeout);
    }

    boost::capy::task<boost::capy::results<>> reset_connection()
    {
        // TODO: it is unclear what should we do here:
        //   We may run DISCARD ALL to clean up evth. except transaction blocks:
        //      CLOSE ALL: cursors
        //      SET SESSION AUTHORIZATION DEFAULT: if you were superuser and impersonated someone
        //        else
        //      RESET ALL: if you changed parameters
        //      DEALLOCATE ALL: if you used explicit PREPARE (you shouldn't)
        //      UNLISTEN *: if you were using this connection to listen
        //      SELECT pg_advisory_unlock_all(): locks
        //      DISCARD PLANS: cached execution plans
        //      DISCARD TEMP: temporary tables
        //      DISCARD SEQUENCES: sequences
        //   If we are within a failed transaction, we could try to detect it
        //   by storing this info upon receiving ready_for_query and running a rollback
        // For now we do nothing, but this should change.

        co_return {};
    }

public:
    pool_node(
        boost::capy::execution_context& ctx,
        const pool_params* params,
        pool_shared_state<pool_node>& shared_st
    )
        : params_(params), shared_st_(&shared_st), conn_(ctx)
    {
        // There is no explicit PING command, but sending a sync will cause
        // the server to answer with ready_for_query
        ping_req_.add(protocol::sync{});
    }

    void mark_as_in_use() noexcept { exiting_idle(); }

    void mark_as_collectable(bool should_reset)
    {
        collection_state_ = should_reset ? collection_state::needs_collect_with_reset
                                         : collection_state::needs_collect;
        collection_ev_.set();
    }

    boost::capy::task<boost::capy::results<>> run()
    {
        const auto tok = co_await boost::capy::this_coro::stop_token;

        // Allow run() track how many of us are outstanding
        // TODO: this shouldn't really be here, but in capy::async_run()'s callback
        auto guard = shared_st_->running_connections_counter.scoped_notify_start();

        // We're now pending.
        // TODO: there is a race condition between create_connection() and running this that can cause
        //    more connections to be created than required.
        entering_pending();

        while (!tok.stop_requested())
        {
            // If there is any leftover, clean it up. This is a best effort - discard the result
            if (conn_.is_connected())
                static_cast<void>(co_await do_shutdown());

            // Try to connect
            if (auto [ec] = co_await do_connect(); ec)
            {
                // If we failed, sleep and try again
                static_cast<void>(co_await boost::corosio::delay(params_->retry_interval));
                continue;
            }

            // We've succeeded and are now idle
            exiting_pending();
            entering_idle();

            while (true)
            {
                // Wait until either it's time to ping, or we're notified for a collection.
                // Note that the idle => in-use transition is performed outside this coro in mark_as_in_use()
                auto [ec] = co_await run_with_timeout(collection_ev_.wait(), params_->ping_interval);
                collection_ev_.clear();

                if (ec == boost::capy::cond::canceled)
                    co_return {};
                else if (is_idle())
                {
                    // We're still idle after the wait. Time to ping
                    exiting_idle();
                    entering_pending();
                    if (auto [ec] = co_await do_ping(); ec)
                    {
                        // Ping failed. Reconnect
                        break;
                    }

                    // Ping succeeded. We're idle again
                    exiting_pending();
                    entering_idle();
                }
                else if (collection_state_ == collection_state::none)
                {
                    // The user is still using the connection. Go back to waiting
                    continue;
                }
                else if (collection_state_ == collection_state::needs_collect)
                {
                    // The user said we don't need a reset. Mark it as idle and keep going
                    entering_idle();
                    collection_state_ = collection_state::none;
                    continue;
                }
                else
                {
                    // The user has finished with the connection but requested a reset
                    BOOST_ASSERT(collection_state_ == collection_state::needs_collect_with_reset);
                    entering_pending();
                    if (auto [ec] = co_await run_with_timeout(reset_connection(), params_->ping_timeout); ec)
                    {
                        // Reset failed. Reconnect
                        break;
                    }

                    // Reset successful, back to idle
                    exiting_pending();
                    entering_idle();
                    continue;
                }
            }
        }

        co_return {};
    }

    // Getters, used by pooled_connection
    co_connection& connection() noexcept { return conn_; }
};

}  // namespace nativepg::detail

#endif
