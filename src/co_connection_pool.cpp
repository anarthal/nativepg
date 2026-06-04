//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/capy/io_task.hpp>

#include <memory>
#include <utility>

#include "nativepg/co_connection.hpp"
#include "nativepg/co_connection_pool.hpp"
#include "nativepg_internal/connection_pool/connection_node.hpp"
#include "nativepg_internal/connection_pool/connection_pool_impl.hpp"

using namespace nativepg;

co_connection_pool::co_connection_pool(boost::capy::execution_context& ctx, pool_params&& params, int)
    : impl_(std::make_unique<detail::co_connection_pool_impl>(ctx, std::move(params)))
{
}

co_connection_pool& co_connection_pool::operator=(co_connection_pool&&) noexcept = default;

co_connection_pool::~co_connection_pool() = default;

boost::capy::io_task<> co_connection_pool::run() { return impl_->run(); }

boost::capy::io_task<pooled_connection> co_connection_pool::get_connection()
{
    return impl_->get_connection();
}

void detail::return_connection(connection_node& node, bool should_reset)
{
    node.mark_as_collectable(should_reset);
}

co_connection& detail::get_connection(connection_node& node) { return node.connection(); }
