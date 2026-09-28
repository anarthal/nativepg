//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_CO_CONNECTION_HPP
#define NATIVEPG_CO_CONNECTION_HPP

#include <boost/capy/buffers.hpp>
#include <boost/capy/concept/executor.hpp>
#include <boost/capy/ex/execution_context.hpp>
#include <boost/capy/io/any_stream.hpp>
#include <boost/capy/io_task.hpp>

#include <concepts>
#include <memory>
#include <optional>

#include "nativepg/connect_params.hpp"
#include "nativepg/encoding.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/notification_vector.hpp"
#include "nativepg/protocol/connection_state.hpp"
#include "nativepg/request.hpp"
#include "nativepg/responses/response_handler.hpp"

namespace nativepg {

class exec_state;

class co_connection
{
    struct impl;
    std::unique_ptr<impl> impl_;

public:
    explicit co_connection(boost::capy::execution_context& ctx);

    template <class Ex>
        requires(!std::same_as<Ex, co_connection> && boost::capy::Executor<Ex>)
    explicit co_connection(const Ex& ex) : co_connection{ex.context()}
    {
    }

    co_connection(co_connection&&) noexcept;
    co_connection(const co_connection&) = delete;

    co_connection& operator=(co_connection&&) noexcept;
    co_connection& operator=(const co_connection&) = delete;

    ~co_connection();

    boost::capy::io_task<> connect(connect_params params, diagnostics* diag = nullptr);

    // Closes the connection (PQfinish)
    boost::capy::io_task<> shutdown();

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

    // Waits until either a notification arrives, or an error occurs.
    // The connection must be in established state.
    // Received notifications are stored in output, which is cleared first.
    // Can be called in parallel with other exec() operations.
    // Only one receive() operation is allowed to be in-flight at any given time.
    //   Issuing another fails with client_errc::already_running.
    // If at least one notification is read, returns a non-empty error code.
    //   If an error condition is detected after some notifications have been read,
    //   they are returned, and no error is reported.
    boost::capy::io_task<> receive(notification_vector& output);

    // Low-level exec API
    boost::capy::io_task<> register_request(
        exec_state& st,
        const request& req,
        response_handler_ref handler,
        diagnostics* diag = nullptr
    );
    boost::capy::io_task<> write_request(exec_state& st);
    boost::capy::io_task<> read_some_response(exec_state& st);

    // Reads until there is at least one message in the read buffer.
    // Access messages with state().read_buffer
    boost::capy::io_task<> read_some_messages();

    // TODO: I don't like this
    boost::capy::any_stream& stream();
    protocol::connection_state& state();

    // Values reported via ParameterStatus
    std::optional<bool> standard_conforming_strings() const;
    std::optional<encoding> client_encoding() const;
};

}  // namespace nativepg

#endif
