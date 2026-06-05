//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/capy/error.hpp>

#include <system_error>

#include "nativepg/co_multiplexed_connection.hpp"
#include "nativepg_internal/multiplexed_connection/multiplexer.hpp"

namespace nativepg {

struct co_multiplexed_connection::impl
{
    detail::multiplexer mpx;

    boost::capy::io_task<> exec(const request& req, response_handler_ref handler, diagnostics* diag = nullptr)
    {
        // Setup
        boost::capy::async_event done_event;
        std::error_code result_ec;
        auto on_done = [&result_ec, &done_event](std::error_code ec) {
            result_ec = ec;
            done_event.set();
        };

        // Add the request to the multiplexer
        auto* elm = mpx.add(&req, handler, on_done);

        // TODO: signal the writer that it has job to be done

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

}  // namespace nativepg
