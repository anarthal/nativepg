//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/capy/results.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/when_all.hpp>

#include <memory>
#include <utility>

#include "nativepg/co_connection.hpp"
#include "nativepg/co_connection_pool.hpp"
#include "nativepg_internal/connection_pool/check_pool_params.hpp"
#include "nativepg_internal/connection_pool/dedicated_pool.hpp"
#include "nativepg_internal/connection_pool/multiplexed_pool.hpp"
#include "nativepg_internal/connection_pool/pool_node.hpp"

using namespace nativepg;

struct nativepg::co_connection_pool::impl
{
    pool_params params;
    detail::dedicated_pool dedicated;
    detail::multiplexed_pool multiplexed;

    // Checks the params before the other constructors run
    static pool_params check_and_return(pool_params&& params)
    {
        detail::check_pool_params(params);
        return std::move(params);
    }

    impl(boost::capy::execution_context& ctx, pool_params&& in_params)
        : params(check_and_return(std::move(in_params))), dedicated(ctx, &params), multiplexed(ctx, &params)
    {
    }

    boost::capy::task<boost::capy::results<>> run()
    {
        auto [ec, dummy1, dummy2] = co_await boost::capy::when_all(dedicated.run(), multiplexed.run());
        co_return {ec};
    }
};

co_connection_pool::co_connection_pool(boost::capy::execution_context& ctx, pool_params&& params, int)
    : impl_(std::make_unique<impl>(ctx, std::move(params)))
{
}

co_connection_pool& co_connection_pool::operator=(co_connection_pool&&) noexcept = default;

co_connection_pool::~co_connection_pool() = default;

boost::capy::task<boost::capy::results<>> co_connection_pool::run() { return impl_->run(); }

boost::capy::task<boost::capy::results<pooled_connection>> co_connection_pool::get_connection()
{
    return impl_->dedicated.get_connection();
}

boost::capy::task<boost::capy::results<>> co_connection_pool::exec(
    const request& req,
    response_handler_ref handler,
    diagnostics* diag
)
{
    return impl_->multiplexed.exec(req, handler, diag);
}

void detail::return_connection(dedicated_pool_node& node, bool should_reset)
{
    // Reset any exec_state's still pointing into the connection.
    // This must be done here for thread-safety, because it writes into the exec_state's,
    // not owned by the pool.
    node.connection().reset_exec_states();

    // Notify the pool that the connection can be collected
    node.mark_as_collectable(should_reset);
}

co_connection& detail::get_connection(dedicated_pool_node& node) { return node.connection(); }
