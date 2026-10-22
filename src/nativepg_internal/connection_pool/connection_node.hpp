//
// Copyright (c) 2019-2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_CONNECTION_NODE_HPP
#define NATIVEPG_CONNECTION_NODE_HPP

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
#include "nativepg_internal/connection_pool/sansio_connection_node.hpp"
#include "nativepg_internal/op_counter.hpp"

namespace nativepg::detail {

// State shared between connection tasks
// TODO: logging
template <class Node>
struct conn_shared_state
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

// The templated type is never exposed to the user. We template
// so tests can inject mocks.
class connection_node : public boost::intrusive::list_base_hook<>,
                        public sansio_connection_node<connection_node>
{
    const pool_params* params_;
    request ping_req_;
    conn_shared_state<connection_node>* shared_st_;
    co_connection conn_;
    boost::capy::async_event collection_ev_;  // Notifications about collections
    collection_state collection_state_{collection_state::none};

    // Hooks for sansio_connection_node
    friend class sansio_connection_node<connection_node>;
    void entering_idle()
    {
        shared_st_->idle_list.push_back(*this);
        shared_st_->idle_connections_cv.notify_one();
    }
    void exiting_idle() { shared_st_->idle_list.erase(shared_st_->idle_list.iterator_to(*this)); }
    void entering_pending() { ++shared_st_->num_pending_connections; }
    void exiting_pending() { --shared_st_->num_pending_connections; }

    boost::capy::task<boost::capy::results<>> run_with_timeout(
        boost::capy::task<boost::capy::results<>> task,
        std::chrono::steady_clock::duration dur
    )
    {
        if (dur.count() > 0)
        {
            co_return co_await boost::corosio::timeout(std::move(task), dur);
        }
        else
        {
            co_return co_await std::move(task);
        }
    }

public:
    connection_node(
        boost::capy::execution_context& ctx,
        const pool_params* params,
        conn_shared_state<connection_node>& shared_st
    )
        : params_(params), shared_st_(&shared_st), conn_(ctx)
    {
        // There is no explicit PING command, but sending a sync will cause
        // the server to answer with ready_for_query
        ping_req_.add(protocol::sync{});
    }

    boost::capy::task<boost::capy::results<>> run()
    {
        // Allow run() track how many of us are outstanding
        auto guard = shared_st_->running_connections_counter.scoped_notify_start();

        // Invoke the sans-io algorithm
        auto last_act_ = resume(std::error_code(), collection_state::none);

        while (true)
        {
            // Check for cancellations. TODO: this is error-prone, consider using a stop callback?
            if ((co_await boost::capy::this_coro::stop_token).stop_requested())
            {
                co_return {};
            }

            // Apply the next action
            switch (last_act_)
            {
                case next_connection_action::connect:
                {
                    auto [ec] = co_await run_with_timeout(
                        conn_.connect(params_->transport),
                        params_->connect_timeout
                    );
                    last_act_ = resume(ec, collection_state::none);
                    break;
                }
                case next_connection_action::sleep_connect_failed:
                {
                    auto [ec] = co_await boost::corosio::delay(params_->retry_interval);
                    last_act_ = resume(ec, collection_state::none);
                    break;
                }
                case next_connection_action::ping:
                {
                    check handler;
                    auto [ec] = co_await run_with_timeout(
                        conn_.exec(ping_req_, &handler),
                        params_->ping_timeout
                    );
                    last_act_ = resume(ec, collection_state::none);
                    break;
                }
                case next_connection_action::reset:
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
                    last_act_ = resume(std::error_code(), collection_state::none);
                    break;
                }
                case next_connection_action::idle_wait:
                {
                    auto [ec] = params_->ping_interval.count() > 0 ? co_await boost::corosio::timeout(
                                                                         collection_ev_.wait(),
                                                                         params_->ping_interval
                                                                     )
                                                                   : co_await collection_ev_.wait();
                    collection_ev_.clear();
                    // TODO: now that this is not MT-safe, the exchange could probably be done in the sans-io
                    // code
                    last_act_ = resume(ec, std::exchange(collection_state_, collection_state::none));
                    break;
                }
                case next_connection_action::none:
                {
                    co_return {};
                }
                default: BOOST_ASSERT(false);  // LCOV_EXCL_LINE
            }
        }
    }

    void mark_as_collectable(bool should_reset)
    {
        collection_state_ = should_reset ? collection_state::needs_collect_with_reset
                                         : collection_state::needs_collect;
        collection_ev_.set();
    }

    // Getters, used by pooled_connection
    co_connection& connection() noexcept { return conn_; }
};

}  // namespace nativepg::detail

#endif
