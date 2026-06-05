//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/capy/buffers/make_buffer.hpp>
#include <boost/capy/cond.hpp>
#include <boost/capy/error.hpp>
#include <boost/capy/ex/async_event.hpp>
#include <boost/capy/ex/this_coro.hpp>
#include <boost/capy/io_task.hpp>
#include <boost/capy/write.hpp>

#include <system_error>

#include "nativepg/co_connection.hpp"
#include "nativepg/co_multiplexed_connection.hpp"
#include "nativepg/protocol/read_message_fsm.hpp"
#include "nativepg_internal/multiplexed_connection/multiplexer.hpp"

namespace capy = boost::capy;

struct nativepg::co_multiplexed_connection::impl
{
    co_connection conn;
    detail::multiplexer mpx;
    capy::async_event write_evt;

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
                    co_return {ec};
                continue;
            }

            // Write the request.
            // TODO: this will have to change once we implement health checks
            auto [ec, bytes] = co_await capy::write(stream, capy::make_buffer(buff));
            if (ec == capy::cond::canceled)
                co_return {ec};
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
                co_return {act.error()};

            // We have a message, deliver it.
            // An error here means an irrecoverable failure
            if (auto ec = mpx.on_message(act.message()))
                co_return {ec};
        }
    }

    boost::capy::io_task<> exec(const request& req, response_handler_ref handler, diagnostics* diag = nullptr)
    {
        // TODO: check request

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
        // If the event wasn't set, we were cancelled (checking this is more reliable that checking wait_ec,
        // because there may be a reschedule between cancellation and resuming, while the callback is sync).
        // On cancellation, elm is valid and should be marked as cancelled.
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
