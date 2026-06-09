//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

// Inspired by Boost.Redis multiplexer.
// Many thanks Marcelo Zimbres Silva for the original design.

#ifndef NATIVEPG_MULTIPLEXER_HPP
#define NATIVEPG_MULTIPLEXER_HPP

#include <boost/compat/function_ref.hpp>

#include <algorithm>
#include <cstddef>
#include <deque>
#include <optional>
#include <span>
#include <system_error>
#include <vector>

#include "nativepg/client_errc.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/protocol/any_backend_message.hpp"
#include "nativepg/protocol/read_response_fsm.hpp"
#include "nativepg/response_handler.hpp"

namespace nativepg {

class request;

namespace detail {

struct multiplexer_elem
{
    const request* req;
    response_handler_ref res;
    boost::compat::function_ref<void(std::error_code)> on_done;  // TODO: do we have any alternative?
    bool abandoned_before_write{};
};

class multiplexer
{
public:
    multiplexer() = default;

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
        elem->req = nullptr;
        elem->res = null_handler_;
        elem->on_done = &ignore;
    }

    std::span<const unsigned char> prepare_write()
    {
        write_buffer_.clear();

        // Go over all pending elements, add them to the write buffer, and mark them as in-progress
        // TODO: ideally, we shouldn't need to copy the payload, but cancellations get much trickier
        for (auto& elm : pending_requests())
        {
            if (elm.req)  // ignore abandoned requests
            {
                auto payload = elm.req->payload();
                write_buffer_.insert(write_buffer_.end(), payload.begin(), payload.end());
            }
            else
            {
                elm.abandoned_before_write = true;
            }
        }

        // All the elements are now in-progress
        num_pending_ = 0u;

        return write_buffer_;
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
        // TODO: handle cancellation
        using protocol::detail::read_response_fsm_impl;
        if (!fsm_.has_value())
        {
            // We're starting a new message. Ignore any abandoned
            // requests that are not expecting any message back
            auto it = std::find_if(elems_.begin(), elems_.end(), [](const multiplexer_elem& elm) {
                return !elm.abandoned_before_write;
            });
            elems_.erase(elems_.begin(), it);

            // If we have no request, something went extremely wrong
            if (elems_.empty())
                return boost::system::error_code(client_errc::unmatched_request);

            // Start the new message
            const auto& elm = elems_.front();
            fsm_.emplace(elm.req, elm.res);
        }

        // Handle the message
        auto res = fsm_->resume(msg);

        // If the FSM terminates, it means we're done with this request
        if (res.type == read_response_fsm_impl::result_type::done)
        {
            elems_.front().on_done(res.ec);
            elems_.pop_front();
            fsm_ = {};
        }

        // Any errors here are protocol violations and should cause connection teardown
        return res.ec;
    }

    // To be called when connection is lost.
    // Cancels requests that are in flight (i.e. not pending)
    void cancel_in_flight()
    {
        // Cancel all the requests
        for (auto& elm : in_flight_requests())
            elm.on_done(std::make_error_code(std::errc::operation_canceled));

        // Remove them
        elems_.erase(elems_.begin(), elems_.begin() + pending_offset());
    }

private:
    class null_handler
    {
        extended_error err_;

    public:
        null_handler() = default;
        handler_setup_result setup(const request& req, std::size_t) { return req.messages().size(); }
        void on_message(const any_request_message&, std::size_t) {}
        const extended_error& result() const { return err_; }
    };

    std::vector<unsigned char> write_buffer_;
    std::deque<multiplexer_elem> elems_;
    null_handler null_handler_;
    std::size_t num_pending_{};

    // TODO: I don't like this
    std::optional<protocol::detail::read_response_fsm_impl> fsm_;

    inline static void ignore(std::error_code) {}

    // Gets the offset in the deque where the pending requests start
    std::size_t pending_offset() const { return elems_.size() - num_pending_; }

    // Gets a view containing all the pending requests. They are at the end
    // of the queue.
    std::ranges::subrange<std::deque<multiplexer_elem>::iterator> pending_requests()
    {
        return std::ranges::subrange(elems_.begin() + pending_offset(), elems_.end());
    }

    // Same, for in-flight requests
    std::ranges::subrange<std::deque<multiplexer_elem>::iterator> in_flight_requests()
    {
        return std::ranges::subrange(elems_.begin(), elems_.begin() + pending_offset());
    }
};

}  // namespace detail
}  // namespace nativepg

#endif  // BOOST_REDIS_MULTIPLEXER_HPP
