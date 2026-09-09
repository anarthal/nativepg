//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

// Benchmark-only variant of co_multiplexed_connection that does not coalesce
// pending requests into a single write buffer: each request is written with a
// separate write call, directly from the request's own payload (no copy).
// Everything else is identical to co_multiplexed_connection. Don't use this in
// production code: because request payloads are not copied, a request must stay
// alive while its write is in progress, so cancelling a request that is being
// written is not supported.

#ifndef NATIVEPG_CO_MULTIPLEXED_CONNECTION_NOCOAL_HPP
#define NATIVEPG_CO_MULTIPLEXED_CONNECTION_NOCOAL_HPP

#include <boost/capy/concept/executor.hpp>
#include <boost/capy/ex/execution_context.hpp>
#include <boost/capy/io_task.hpp>

#include <concepts>
#include <memory>
#include <vector>

#include "nativepg/co_multiplexed_connection.hpp"  // multiplexed_config
#include "nativepg/extended_error.hpp"
#include "nativepg/notification_event.hpp"
#include "nativepg/request.hpp"
#include "nativepg/responses/response_handler.hpp"

namespace nativepg {

class co_multiplexed_connection_nocoal
{
    struct impl;
    std::unique_ptr<impl> impl_;

public:
    explicit co_multiplexed_connection_nocoal(boost::capy::execution_context& ctx);

    template <class Ex>
        requires(!std::same_as<Ex, co_multiplexed_connection_nocoal> && boost::capy::Executor<Ex>)
    explicit co_multiplexed_connection_nocoal(const Ex& ex)
        : co_multiplexed_connection_nocoal{ex.context()}
    {
    }

    co_multiplexed_connection_nocoal(co_multiplexed_connection_nocoal&&) noexcept;
    co_multiplexed_connection_nocoal(const co_multiplexed_connection_nocoal&) = delete;

    co_multiplexed_connection_nocoal& operator=(co_multiplexed_connection_nocoal&&) noexcept;
    co_multiplexed_connection_nocoal& operator=(const co_multiplexed_connection_nocoal&) = delete;

    ~co_multiplexed_connection_nocoal();

    boost::capy::io_task<> run(multiplexed_config cfg);

    boost::capy::io_task<> exec(
        const request& req,
        response_handler_ref handler,
        diagnostics* diag = nullptr
    );

    template <response_handler ResponseHandler>
    boost::capy::io_task<> exec(const request& req, ResponseHandler handler, diagnostics* diag = nullptr)
    {
        // Keep the handler alive
        co_return co_await exec(req, response_handler_ref(&handler), diag);
    }

    boost::capy::io_task<> read_notifications(std::vector<notification_event>& output);
};

}  // namespace nativepg

#endif
