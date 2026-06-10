//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/capy/buffers/make_buffer.hpp>
#include <boost/capy/cond.hpp>
#include <boost/capy/delay.hpp>
#include <boost/capy/error.hpp>
#include <boost/capy/ex/async_event.hpp>
#include <boost/capy/ex/this_coro.hpp>
#include <boost/capy/io_task.hpp>
#include <boost/capy/when_any.hpp>
#include <boost/capy/write.hpp>

#include <memory>
#include <system_error>
#include <utility>

#include "nativepg/co_connection.hpp"
#include "nativepg/co_multiplexed_connection.hpp"
#include "nativepg/protocol/any_backend_message.hpp"
#include "nativepg/protocol/read_message_fsm.hpp"
#include "nativepg_internal/check_request.hpp"
#include "nativepg_internal/multiplexed_connection/multiplexer.hpp"
#include "nativepg_internal/notify_queue.hpp"

namespace capy = boost::capy;

struct nativepg::co_multiplexed_connection::impl
{
    co_connection conn;
    detail::multiplexer mpx;
    capy::async_event write_evt;
    detail::notify_queue notif_queue{256u};  // TODO: make configurable

    explicit impl(boost::capy::execution_context& ctx) : conn(ctx) {}

    // These tasks don't return an error code so when_any
    // finishes when they return
    capy::io_task<> writer()
    {
        auto& stream = conn.stream();

        while (true)
        {
            // Attempt to prepare any pending requests
            auto buff = mpx.prepare_write();

            // No more requests to write. Wait for more
            if (buff.empty())
            {
                write_evt.clear();
                auto [ec] = co_await write_evt.wait();
                if (ec == capy::cond::canceled)
                    co_return {};
                continue;
            }

            // Write the request.
            // TODO: this will have to change once we implement health checks
            auto [ec, bytes] = co_await capy::write(stream, capy::make_buffer(buff));
            if (ec)
                co_return {};
        }
    }

    capy::io_task<> reader()
    {
        auto& stream = conn.stream();
        auto& st = conn.state();

        while (true)
        {
            // Read one entire message
            auto act = st.read_msg_stream_fsm.resume(st, {}, 0u);
            while (act.type() == protocol::read_message_stream_fsm::result_type::read)
            {
                auto [ec, bytes] = co_await stream.read_some(boost::capy::make_buffer(act.read_buffer()));
                act = st.read_msg_stream_fsm.resume(st, ec, bytes);
            }

            // An error reading a message indicates an irrecoverable failure
            if (act.type() == protocol::read_message_stream_fsm::result_type::error)
                co_return {};

            // Handle notifications
            // TODO: although this is a valid backpressure strategy,
            // it interacts poorly with running requests in the same coroutine.
            // This is known in Boost.Redis. I'd like to make it better. Options include
            //    a. Let exec() and read_notifies() handle run()'s work. This buys built-in
            //       backpressure, but eliminates the option of built-in health-checks.
            //    b. Make exec() unblock the reader if it's waiting for a message.
            //       Makes the upper limit soft. May need to tear down the connection
            //       if too many notifications stack.
            if (act.message().type() == protocol::any_backend_message::kind::notification_response)
            {
                const auto& notif_msg = act.message().get_notification_response();
                if (!notif_queue.try_add_notify(notif_msg))
                {
                    if (auto [ec] = co_await notif_queue.add_notify(notif_msg); ec)
                        co_return {ec};
                }
                continue;
            }

            // We have a message, deliver it.
            // An error here means an irrecoverable failure
            if (auto ec = mpx.on_message(act.message()))
                co_return {};
        }
    }

    capy::io_task<> run(multiplexed_config cfg)
    {
        auto tok = co_await capy::this_coro::stop_token;

        while (true)
        {
            // Try to connect
            // TODO: this is doing a copy
            // TODO: are we properly resetting state here?
            auto [ec] = co_await conn.connect(cfg.transport);
            if (tok.stop_requested())
                co_return {capy::error::canceled};

            if (!ec)
            {
                // We connected
                notif_queue.add_connect();

                // Run the tasks
                [[maybe_unused]] auto res = co_await capy::when_any(writer(), reader());
                if (tok.stop_requested())
                    co_return {capy::error::canceled};

                // We've lost connection or otherwise been cancelled.
                // Remove from the multiplexer the required requests.
                // TODO: we should ensure that no request is allowed to enter once we're cancelled
                mpx.cleanup();

                // Notify listeners that we've disconnected
                notif_queue.add_disconnect();
            }

            // Wait for the reconnection interval
            auto [ec_wait] = co_await capy::delay(cfg.reconnect_wait_interval);
            if (ec_wait)  // only possible failure should be cancellation
                co_return {ec_wait};
        }
    }

    boost::capy::io_task<> exec(const request& req, response_handler_ref handler, diagnostics* diag = nullptr)
    {
        // Check that the request is valid
        if (auto req_ec = protocol::detail::setup_request(req, handler))
            co_return {req_ec};
        // TODO: diagnostics

        // Setup
        boost::capy::async_event done_event;
        std::error_code result_ec;
        auto on_done = [&result_ec, &done_event](std::error_code ec) {
            result_ec = ec;
            done_event.set();
        };

        // Add the request to the multiplexer
        auto* elm = mpx.add(&req, handler, on_done);

        // Signal the writer that it has job to be done
        write_evt.set();

        // Wait for the response to arrive
        auto [wait_ec] = co_await done_event.wait();
        static_cast<void>(wait_ec);

        // We're done. If the event was set, the request has completed without cancellations.
        // No cleanup is required. The element will probably be invalid at this point (elm dangles)
        // If the event wasn't set, we were cancelled (checking this is more reliable that checking
        // wait_ec, because there may be a reschedule between cancellation and resuming, while the
        // callback is sync). On cancellation, elm is valid and should be marked as cancelled.
        if (done_event.is_set())
        {
            co_return {result_ec ? result_ec : std::error_code(handler.result().code)};
        }
        else
        {
            mpx.cancel(elm);
            co_return {boost::capy::error::canceled};
        }
    }
};

nativepg::co_multiplexed_connection::co_multiplexed_connection(boost::capy::execution_context& ctx)
    : impl_(std::make_unique<impl>(ctx))
{
}

nativepg::co_multiplexed_connection& nativepg::co_multiplexed_connection::operator=(
    co_multiplexed_connection&&
) noexcept = default;

nativepg::co_multiplexed_connection::~co_multiplexed_connection() = default;

boost::capy::io_task<> nativepg::co_multiplexed_connection::run(multiplexed_config cfg)
{
    return impl_->run(std::move(cfg));
}

boost::capy::io_task<> nativepg::co_multiplexed_connection::exec(
    const request& req,
    response_handler_ref handler,
    diagnostics* diag
)
{
    return impl_->exec(req, handler, diag);
}

boost::capy::io_task<> nativepg::co_multiplexed_connection::read_notifies(std::vector<notify_event>& output)
{
    return impl_->notif_queue.read_events(output);
}
