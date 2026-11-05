//
// Copyright (c) 2019-2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_CO_CONNECTION_POOL_HPP
#define NATIVEPG_CO_CONNECTION_POOL_HPP

#include <boost/assert.hpp>
#include <boost/capy/concept/executor.hpp>
#include <boost/capy/ex/execution_context.hpp>
#include <boost/capy/results.hpp>
#include <boost/capy/task.hpp>

#include <chrono>
#include <memory>
#include <utility>

#include "nativepg/co_connection.hpp"
#include "nativepg/connect_params.hpp"

namespace nativepg {

namespace detail {
class pool_node;
class co_connection_pool_impl;
void return_connection(pool_node&, bool should_reset);
co_connection& get_connection(pool_node&);
}  // namespace detail

// Configuration for the pool
struct pool_params
{
    // How to reach the server
    connect_params transport;

    // The initial number of dedicated connections.
    // When run() is called, this number of connections will be initially created.
    // (Think of this as the minimum number of connections that a pool may have).
    std::size_t initial_size{1};

    // The maximum number of connections. If a connection is requested, but none are available,
    // a new connection will be established, up to this size.
    // TODO: update this number, this is MySQL's default.
    std::size_t max_size{151};

    // Timeout for connection establishment (physical connect, TLS handshake, PostgreSQL session setup).
    // Set it to zero to disable the timeout.
    std::chrono::steady_clock::duration connect_timeout{std::chrono::seconds(20)};

    // Time that we wait after a connection establishment failure, before we try again.
    std::chrono::steady_clock::duration retry_interval{std::chrono::seconds(30)};

    // Time interval that a connection must be idle (i.e. not given to the user)
    // before a dummy request is sent.
    // Set it to zero to disable pings.
    std::chrono::steady_clock::duration ping_interval{std::chrono::seconds(30)};

    // Timeout for pings and resets.
    // Set it to zero to disable the timeout.
    std::chrono::steady_clock::duration ping_timeout{std::chrono::seconds(10)};
};

// Type returned by get_connection. Acts as a unique handle into a connection.
// Once the handle is destroyed, the connection is returned to the pool.
class pooled_connection
{
    struct deleter
    {
        void operator()(detail::pool_node* p) const { detail::return_connection(*p, true); }
    };

    std::unique_ptr<detail::pool_node, deleter> node_;

    explicit pooled_connection(detail::pool_node& node) noexcept : node_(&node) {}

    friend class detail::co_connection_pool_impl;

public:
    // Constructs an empty object, wrapping no connection
    pooled_connection() noexcept = default;

    // Move operations have the usual move semantics for unique ownership types
    pooled_connection(pooled_connection&& other) = default;
    pooled_connection& operator=(pooled_connection&& other) noexcept = default;

    // The type is not copyable
    pooled_connection(const pooled_connection&) = delete;
    pooled_connection& operator=(const pooled_connection&) = delete;

    // Destroying a pooled connection returns it to the pool, if the object
    // is valid
    ~pooled_connection() = default;

    // Does the object point to a valid connection? Or is it 'empty'? I.e. default
    // constructed, moved-from or already returned.
    bool valid() const noexcept { return node_ != nullptr; }

    // Retrieves the underlying connection
    // Precondition: `valid() == true`
    co_connection& get() noexcept { return detail::get_connection(*node_); }
    const co_connection& get() const noexcept { return detail::get_connection(*node_); }
    co_connection* operator->() noexcept { return &get(); }
    const co_connection* operator->() const noexcept { return &get(); }

    // Returns the connection to the pool, asking to skip reset.
    // Precondition: `is_valid() == true`
    // After this function returns, the object is invalid.
    void return_without_reset() noexcept
    {
        BOOST_ASSERT(valid());
        detail::return_connection(*node_.release(), false);
    }
};

// A pool of dedicated connections.
//
// Running the pool: for the pool to work you must call run().
//   It establishes connections and keeps them healthy.
//   It must be called exactly once per pool.
// Connection creation: performed by run(). At first,
//   pool_params::initial_size connections are created.
//   When a connection is requested by get_connection(), and no
//   connection is available or in the process of being established,
//   run() creates a new connection.
//   This happens until pool_params::max_size connections are created.
//   Further requests wait until a connection is returned to the pool.
//   Connections are created using the execution_context passed to the pool's constructor.
// Connection retrieval: you get a connection calling get_connection.
//   If no connection is available (e.g. all of them are in use or the server is unhealthy),
//   blocks indefinitely. You may want to set a timeout on this function.
//   Once a healthy connection is retrieved, the pool won't execute any operations
//   on it until you return it to the pool.
// Returning a connection to the pool: through pooled_connection's functions.
//   There are two modalities:
//     - With reset: asks the pool to perform cleanup on the connection before
//       handing it to other sessions. Safer but slower, because it requires
//       a round-trip to the server. Triggered by ~pooled_connection() and assigning to pooled_connection.
//     - Without reset: asks the pool to skip the reset step. Use it as an optimization,
//       when you know that you didn't mutate session state. E.g. if you ran a SELECT
//       that only returns data, this is safe. If you acquired an advisory lock, it is not.
//       Triggered by return_without_reset.
//       Use this semantic only when you're completely sure that your session didn't
//       leave anything behind. As a rule of thumb, if you encountered an error, don't
//       call return_without_reset.
//   All async operations on the underlying connection must complete before returning
//   the connection to the pool (i.e. `has_ops_in_flight() == false`). Otherwise, the
//   behavior is undefined.
// Establishing a connection: performed in the background by run(), using co_connection::connect.
//   If session establishment fails or times out, pool_params::retry_interval is waited before
//   trying again. There is no retry limit.
// Keeping a connection healthy: if a connection hasn't been handed to the user
//   for pool_params::ping_interval, a ping is issued. In the current implementation,
//   a ping consists of a Sync message, but this may change. The connection won't be handed
//   to the user while pinging. Pings don't run while the connection is in-use.
// Resetting the connection: once a connection is returned, the pool tries to
//   clean up session state as much as possible. The pool may respond to you returning
//   a connection with the following actions (recall that these happen in the background, owned by run()):
//     - Nothing. If the pool evaluates that the connection is clean, it marks it as available immediately.
//       Cached notifications are discarded
//       (as per clear_cached_notifications() - this doesn't entail server round-trip).
//     - Session-reset. If the pool thinks that the connection works but its state has been modified,
//       it attempts to clean it up. This currently entails a `ROLLBACK` if a transaction block is open,
//       a `DISCARD ALL`, and a call to clear_cached_notifications().
//       This is lighter than re-establishing the connection, but requires a round-trip to the server.
//     - Re-establishing the connection. This is the most drastic (and heavyweight) action.
//   co_connection has some functions that indicate how clean a session is. The pool currently
//   looks for the following (although this may be expanded in the future):
//     - txn_status() is expected to be transaction_status::idle.
//     - is_connected() is expected to be true.
//     - has_leftovers() is expected to be false.
//   The decision is made as follows:
//     - If you used return_without_reset(), and the above indicators are clean, nothing is reset.
//     - If you didn't use return_without_reset(), and the above indicators are clean, a reset is issued.
//     - If the above indicators are not clean, either a reset or a re-establishment is performed,
//       regardless of what returning method you used. The exact logic is not specified.
//
class co_connection_pool
{
    std::unique_ptr<detail::co_connection_pool_impl> impl_;

    co_connection_pool(boost::capy::execution_context& ctx, pool_params&& params, int);

public:
    // Throws if the params are invalid (e.g. negative timeouts).
    co_connection_pool(boost::capy::execution_context& ctx, pool_params params)
        : co_connection_pool(ctx, std::move(params), 0)
    {
    }

    template <boost::capy::Executor Ex>
    co_connection_pool(const Ex& ex, pool_params params)
        : co_connection_pool(ex.context(), std::move(params), 0)
    {
    }

    co_connection_pool(const co_connection_pool&) = delete;
    co_connection_pool& operator=(const co_connection_pool&) = delete;

    co_connection_pool(co_connection_pool&& other) = default;

    co_connection_pool& operator=(co_connection_pool&& other) noexcept;

    ~co_connection_pool();

    bool valid() const noexcept { return impl_.get() != nullptr; }

    boost::capy::task<boost::capy::results<>> run();

    boost::capy::task<boost::capy::results<pooled_connection>> get_connection();
};

}  // namespace nativepg

#endif
