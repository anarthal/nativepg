//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_CO_MULTIPLEXED_CONNECTION_HPP
#define NATIVEPG_CO_MULTIPLEXED_CONNECTION_HPP

#include <boost/capy/concept/executor.hpp>
#include <boost/capy/ex/execution_context.hpp>
#include <boost/capy/io_task.hpp>

#include <concepts>
#include <memory>
#include <vector>

#include "nativepg/connect_params.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/notify_event.hpp"
#include "nativepg/request.hpp"
#include "nativepg/response_handler.hpp"

namespace nativepg {

struct multiplexed_config
{
    connect_params transport;
    // TODO: timeouts
    // TODO: health checks

    /// Time span to wait between successive connection retries.
    std::chrono::steady_clock::duration reconnect_wait_interval = std::chrono::seconds{1};
};

class co_multiplexed_connection
{
    struct impl;
    std::unique_ptr<impl> impl_;

public:
    explicit co_multiplexed_connection(boost::capy::execution_context& ctx);

    template <class Ex>
        requires(!std::same_as<Ex, co_multiplexed_connection> && boost::capy::Executor<Ex>)
    explicit co_multiplexed_connection(const Ex& ex) : co_multiplexed_connection{ex.context()}
    {
    }

    co_multiplexed_connection(co_multiplexed_connection&&) = default;
    co_multiplexed_connection(const co_multiplexed_connection&) = delete;

    co_multiplexed_connection& operator=(co_multiplexed_connection&&) noexcept;
    co_multiplexed_connection& operator=(const co_multiplexed_connection&) = delete;

    ~co_multiplexed_connection();

    boost::capy::io_task<> run(multiplexed_config cfg);

    boost::capy::io_task<> exec(
        const request& req,
        response_handler_ref handler,
        diagnostics* diag = nullptr
    );

    boost::capy::io_task<> read_notifies(std::vector<notify_event>& output);
};

}  // namespace nativepg

#endif
