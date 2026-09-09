//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

// Benchmark-only variant of detail::multiplexer that does not coalesce
// pending requests into a single write buffer. Instead, prepare_write()
// hands out one request payload at a time, pointing directly into the
// request object, so the writer issues one write per request and no copy
// is performed. Everything else (cancellation bookkeeping, response
// dispatching) is identical to the coalescing multiplexer, and the shared
// pieces are reused from multiplexer.hpp.

#ifndef NATIVEPG_MULTIPLEXER_NOCOAL_HPP
#define NATIVEPG_MULTIPLEXER_NOCOAL_HPP

#include <boost/compat/function_ref.hpp>

#include <cstddef>
#include <deque>
#include <ranges>
#include <span>
#include <system_error>

#include "nativepg/protocol/any_backend_message.hpp"
#include "nativepg/request.hpp"
#include "nativepg/responses/check.hpp"
#include "nativepg/responses/response_handler.hpp"
#include "nativepg_internal/multiplexed_connection/multiplexer.hpp"

namespace nativepg {

class request;

namespace detail {

class multiplexer_nocoal
{
public:
    multiplexer_nocoal() = default;

    // Adds a request. To be called by execute
    multiplexer_elem* add(
        const request* req,
        response_handler_ref res,
        boost::compat::function_ref<void(std::error_code)> on_done
    )
    {
        elems_.push_back({req, res, on_done});
        ++num_pending_;
        return &elems_.back();
    }

    void cancel(multiplexer_elem* elem)
    {
        BOOST_ASSERT(elem != nullptr);
        BOOST_ASSERT(!elems_.empty());

        switch (elem->status)
        {
            case multiplexer_elem_status::pending:
            {
                // The request hasn't been written yet.
                // Mark it as abandoned and it will be ignored and removed when possible
                elem->status = multiplexer_elem_status::abandoned_pending;
                break;
            }
            case multiplexer_elem_status::in_flight:
            {
                // We've sent this request. We need to keep enough info to identify
                // the responses for this request and discard them.
                // The process differs if we've already read part of the response
                elem->status = multiplexer_elem_status::abandoned_in_flight;
                if (elem == &elems_.front() && fsm_.is_reading())
                    fsm_.abandon_current();
                else
                    elem->num_rfq = get_expected_rfqs(elem->req->messages());
                break;
            }
            default: BOOST_ASSERT(false); break;
        }

        // In any case, clean up other data members, just in case
        elem->req = nullptr;
        elem->res = &null_handler_;
        elem->on_done = &ignore;
    }

    // Returns the payload of the next request to be written, marking it as in-flight,
    // or an empty span if there is nothing pending. As opposed to the coalescing
    // multiplexer, only one request is returned per call, and the returned span points
    // into the request object rather than into an owned buffer (no copy is made).
    // WARNING: because the payload is not copied, the request must stay alive while the
    // write is in progress. Cancelling a request while its payload is being written is
    // therefore not supported here. This is fine for benchmarking, which never cancels.
    std::span<const unsigned char> prepare_write()
    {
        // Go over the pending elements until we find a healthy one to write
        while (num_pending_ > 0u)
        {
            // The pending elements are at the end of the queue. Take the first one
            // and mark it as no longer pending, so the pending/in-flight boundary advances
            auto& elm = *(elems_.begin() + pending_offset());
            --num_pending_;

            switch (elm.status)
            {
                case multiplexer_elem_status::pending:
                {
                    // Healthy request
                    BOOST_ASSERT(elm.req);
                    elm.status = multiplexer_elem_status::in_flight;
                    return elm.req->payload();
                }
                case multiplexer_elem_status::abandoned_pending:
                {
                    // The request was cancelled before being written, ignore it
                    break;
                }
                default: BOOST_ASSERT(false); break;
            }
        }

        return {};
    }

    [[nodiscard]]
    std::error_code on_message(const protocol::any_backend_message& msg)
    {
        // Handle asynchronous messages
        // TODO: actually do something useful with these
        switch (msg.type())
        {
            case protocol::any_backend_message::kind::notice_response:
            case protocol::any_backend_message::kind::notification_response:
            case protocol::any_backend_message::kind::parameter_status: return std::error_code();
            default: break;
        }

        // The message is supposed to belong to a request, handle it
        return fsm_.on_message(elems_, msg);
    }

    // To be called when connection is lost.
    // Cancels requests that are in flight (i.e. not pending)
    // and resets state
    void cleanup()
    {
        // Cancel all the requests
        for (auto& elm : in_flight_requests())
            elm.on_done(std::make_error_code(std::errc::operation_canceled));

        // Remove them
        elems_.erase(elems_.begin(), elems_.begin() + pending_offset());

        // Clean up state
        fsm_.reset();
    }

private:
    std::deque<multiplexer_elem> elems_;
    check null_handler_;
    std::size_t num_pending_{};
    read_response_stream_fsm fsm_;

    inline static void ignore(std::error_code) {}

    // Gets the offset in the deque where the pending requests start
    std::size_t pending_offset() const { return elems_.size() - num_pending_; }

    // Gets a view containing all the in-flight requests. They are at the beginning
    // of the queue.
    std::ranges::subrange<std::deque<multiplexer_elem>::iterator> in_flight_requests()
    {
        return std::ranges::subrange(elems_.begin(), elems_.begin() + pending_offset());
    }
};

}  // namespace detail
}  // namespace nativepg

#endif
