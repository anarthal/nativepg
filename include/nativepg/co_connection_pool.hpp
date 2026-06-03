//
// Copyright (c) 2019-2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_CO_CONNECTION_POOL_HPP
#define NATIVEPG_CO_CONNECTION_POOL_HPP

#include <boost/capy/concept/executor.hpp>
#include <boost/capy/ex/execution_context.hpp>
#include <boost/capy/io_task.hpp>
#include <boost/compat/function_ref.hpp>

#include <chrono>
#include <memory>
#include <utility>

#include "nativepg/co_connection.hpp"
#include "nativepg/connect_params.hpp"

namespace nativepg {

namespace detail {
struct connection_node;
void return_connection(connection_node&, bool should_reset);
co_connection& get_connection(const connection_node&);
}  // namespace detail

struct pool_params
{
    connect_params transport;
    std::size_t initial_size{1};
    std::size_t max_size{151};
    std::chrono::steady_clock::duration connect_timeout{std::chrono::seconds(20)};
    std::chrono::steady_clock::duration retry_interval{std::chrono::seconds(30)};
    std::chrono::steady_clock::duration ping_interval{std::chrono::seconds(30)};
    std::chrono::steady_clock::duration ping_timeout{std::chrono::seconds(10)};
};

class pooled_connection
{
    detail::connection_node* node_;

    pooled_connection(detail::connection_node& node) noexcept : node_(&node) {}

public:
    pooled_connection() noexcept = default;

    pooled_connection(pooled_connection&& other) noexcept : node_(other.node_) { other.node_ = nullptr; }

    pooled_connection& operator=(pooled_connection&& other) noexcept
    {
        if (valid())
        {
            detail::return_connection(*node_, true);
        }
        node_ = other.node_;
        other.node_ = nullptr;
        return *this;
    }

    pooled_connection(const pooled_connection&) = delete;
    pooled_connection& operator=(const pooled_connection&) = delete;

    ~pooled_connection()
    {
        if (valid())
            detail::return_connection(*node_, true);
    }

    bool valid() const noexcept { return node_ != nullptr; }

    co_connection& get() noexcept { return detail::get_connection(*node_); }

    const co_connection& get() const noexcept { return detail::get_connection(*node_); }

    co_connection* operator->() noexcept { return &get(); }

    const co_connection* operator->() const noexcept { return &get(); }

    void return_without_reset() noexcept
    {
        BOOST_ASSERT(valid());
        detail::return_connection(*node_, false);
        node_ = nullptr;
    }
};

class co_connection_pool
{
    struct impl;
    std::unique_ptr<impl> impl_;

    co_connection_pool(boost::capy::execution_context& ctx, pool_params&& params, int);

public:
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

    boost::capy::io_task<> run();

    boost::capy::io_task<pooled_connection> get_connection();
};

}  // namespace nativepg

#endif
