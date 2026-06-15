//
// Copyright (c) 2019-2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_CONNECTION_NODE_HPP
#define NATIVEPG_CONNECTION_NODE_HPP

#include <boost/capy/delay.hpp>
#include <boost/capy/ex/async_event.hpp>
#include <boost/capy/ex/execution_context.hpp>
#include <boost/capy/ex/this_coro.hpp>
#include <boost/capy/io_task.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/timeout.hpp>
#include <boost/corosio/timer.hpp>
#include <boost/intrusive/list.hpp>
#include <boost/intrusive/list_hook.hpp>

#include <chrono>
#include <system_error>
#include <utility>

#include "nativepg/co_connection.hpp"
#include "nativepg/co_connection_pool.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/protocol/notice_error.hpp"
#include "nativepg/protocol/sync.hpp"
#include "nativepg/request.hpp"
#include "nativepg/sqlstate.hpp"
#include "nativepg_internal/connection_pool/sansio_connection_node.hpp"

namespace nativepg::detail {

// State shared between connection tasks
// TODO: logging
template <class Node>
struct conn_shared_state
{
    // The list of connections that are currently idle. Non-owning.
    boost::intrusive::list<Node> idle_list;

    // Condition variable to wait for idle connections
    boost::corosio::timer idle_connections_cv;  // TODO: this is not the best primitive

    // The number of pending connections (currently getting ready).
    // Required to compute how many connections we should create at any given point in time.
    std::size_t num_pending_connections{0};

    // The number of async_get_connection ops that are waiting for a connection to become available.
    // Required to compute how many connections we should create at any given point in time.
    std::size_t num_pending_requests{0};

    // The number of running connections, to track when they exit
    std::size_t num_running_connections{0};

    // Condition variable to wait for all connections to exit
    boost::capy::async_event conns_finished_cv;

    void on_connection_start() { ++num_running_connections; }

    void on_connection_finish()
    {
        if (--num_running_connections == 0u)
            conns_finished_cv.set();
    }
    explicit conn_shared_state(boost::capy::execution_context& ctx)
        : idle_connections_cv(ctx, (std::chrono::steady_clock::time_point::max)())
    {
    }
};

// TODO: this handler should be part of the public API
class check_handler
{
    extended_error err_{};

public:
    handler_setup_result setup(const request& req, std::size_t offset)
    {
        BOOST_ASSERT(offset == 0u);
        err_ = {};
        return req.messages().size();
    }

    void on_message(const any_request_message& req, std::size_t)
    {
        if (const auto* msg = boost::variant2::get_if<protocol::error_response>(&req))
        {
            err_.code = parse_sqlstate(msg->sqlstate.value_or(std::string_view{}));
            err_.diag.assign(*msg);
        }
    }
    const extended_error& result() const { return err_; }
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
        shared_st_->idle_connections_cv.cancel_one();
    }
    void exiting_idle() { shared_st_->idle_list.erase(shared_st_->idle_list.iterator_to(*this)); }
    void entering_pending() { ++shared_st_->num_pending_connections; }
    void exiting_pending() { --shared_st_->num_pending_connections; }

    boost::capy::io_task<> run_with_timeout(
        boost::capy::io_task<> task,
        std::chrono::steady_clock::duration dur
    )
    {
        if (dur.count() > 0)
        {
            co_return co_await boost::capy::timeout(std::move(task), dur);
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

    boost::capy::io_task<> run()
    {
        // We've just started
        shared_st_->on_connection_start();

        // Invoke the sans-io algorithm
        auto last_act_ = resume(std::error_code(), collection_state::none);

        while (true)
        {
            // Check for cancellations. TODO: this is error-prone, consider using a stop callback?
            if ((co_await boost::capy::this_coro::stop_token).stop_requested())
            {
                shared_st_->on_connection_finish();
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
                    auto [ec] = co_await boost::capy::delay(params_->retry_interval);
                    last_act_ = resume(ec, collection_state::none);
                    break;
                }
                case next_connection_action::ping:
                {
                    check_handler handler;
                    auto [ec] = co_await run_with_timeout(
                        conn_.exec(ping_req_, handler),
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
                    //      else RESET ALL: if you changed parameters DEALLOCATE ALL: if you used explicit
                    //      PREPARE (you shouldn't) UNLISTEN *: if you were using this connection to SELECT
                    //      pg_advisory_unlock_all(): locks DISCARD PLANS: cached execution plans DISCARD
                    //      TEMP: temporary tables DISCARD SEQUENCES: sequences
                    //   If we are within a failed transaction, we could try to detect it
                    //   by storing this info upon receiving ready_for_query and running a rollback
                    // For now we do nothing, but this should change.
                    last_act_ = resume(std::error_code(), collection_state::none);
                    break;
                }
                case next_connection_action::idle_wait:
                {
                    auto [ec] = params_->ping_interval.count() > 0 ? co_await boost::capy::timeout(
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
                    shared_st_->on_connection_finish();
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
