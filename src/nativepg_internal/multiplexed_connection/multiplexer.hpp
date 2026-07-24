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
#include "nativepg/protocol/any_backend_message.hpp"
#include "nativepg/protocol/read_response_fsm.hpp"
#include "nativepg/request.hpp"
#include "nativepg/response.hpp"
#include "nativepg/response_handler.hpp"

namespace nativepg {

class request;

namespace detail {

enum class multiplexer_elem_status
{
    pending,
    in_flight,
    abandoned_pending,
    abandoned_in_flight,
};

struct multiplexer_elem
{
    const request* req;
    response_handler_ref res;
    boost::compat::function_ref<void(std::error_code)> on_done;  // TODO: do we have any alternative?
    multiplexer_elem_status status{multiplexer_elem_status::pending};
    std::size_t num_rfq{};  // Expected number of ready-for-query messages. Populated lazily
};

inline std::size_t get_expected_rfqs(std::span<const request_message_type> msgs)
{
    return std::ranges::count_if(msgs, [](request_message_type type) {
        return type == request_message_type::query || type == request_message_type::sync;
    });
}

class read_response_stream_fsm
{
    enum class status
    {
        initial,
        reading,
        ignoring,
    };

    status status_{status::initial};
    std::optional<protocol::read_response_fsm> fsm_;  // TODO: don't like optional
    std::size_t remaining_rfq_{};

public:
    read_response_stream_fsm() = default;

    void reset() { status_ = status::initial; }

    bool is_reading() const { return status_ == status::reading; }

    [[nodiscard]]
    std::error_code on_message(std::deque<multiplexer_elem>& elms, const protocol::any_backend_message& msg)
    {
        using protocol::read_response_fsm;

        if (status_ == status::initial)
        {
            // We're starting a new message. Ignore any abandoned
            // requests that are not expecting any message back
            auto it = std::ranges::find_if(elms, [](const multiplexer_elem& elm) {
                return elm.status != multiplexer_elem_status::abandoned_pending;
            });
            elms.erase(elms.begin(), it);

            // If we have no request, something went extremely wrong
            if (elms.empty())
                return boost::system::error_code(client_errc::unmatched_request);
            const auto& elm = elms.front();

            // Determine if we should care about responses or not
            if (elm.status == multiplexer_elem_status::abandoned_in_flight)
            {
                status_ = status::ignoring;
                remaining_rfq_ = elm.num_rfq;
            }
            else
            {
                BOOST_ASSERT(elm.status == multiplexer_elem_status::in_flight);
                status_ = status::reading;
                fsm_.emplace(elm.req, elm.res);
            }
        }

        switch (status_)
        {
            case status::reading:
            {
                // Handle the message
                auto res = fsm_->resume(msg);

                // If the FSM terminates, it means we're done with this request
                if (res.type == read_response_fsm::result_type::done)
                {
                    elms.front().on_done(res.ec);
                    elms.pop_front();
                    status_ = status::initial;
                }

                // Any errors here are protocol violations and should cause connection teardown
                return res.ec;
            }
            case status::ignoring:
            {
                // We only care about ready for queries
                BOOST_ASSERT(remaining_rfq_ > 0u);
                if (msg.type() == protocol::any_backend_message::kind::ready_for_query &&
                    --remaining_rfq_ == 0u)
                {
                    elms.pop_front();
                    status_ = status::initial;
                }
                return {};
            }
            default: BOOST_ASSERT(false); return boost::system::error_code(client_errc::unmatched_request);
        }
    }

    void abandon_current()
    {
        BOOST_ASSERT(is_reading());
        remaining_rfq_ = get_expected_rfqs(fsm_->get_remaining_messages());
        status_ = status::ignoring;
    }
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

    std::span<const unsigned char> prepare_write()
    {
        write_buffer_.clear();

        // Go over all pending elements, add them to the write buffer, and mark them as in-progress
        // TODO: ideally, we shouldn't need to copy the payload, but cancellations get much trickier
        for (auto& elm : pending_requests())
        {
            switch (elm.status)
            {
                case multiplexer_elem_status::pending:
                {
                    // Healthy request
                    BOOST_ASSERT(elm.req);
                    auto payload = elm.req->payload();
                    write_buffer_.insert(write_buffer_.end(), payload.begin(), payload.end());
                    elm.status = multiplexer_elem_status::in_flight;
                    break;
                }
                case multiplexer_elem_status::abandoned_pending:
                {
                    // The request was cancelled before being written, ignore it
                    break;
                }
                default: BOOST_ASSERT(false); break;
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
    std::vector<unsigned char> write_buffer_;
    std::deque<multiplexer_elem> elems_;
    check null_handler_;
    std::size_t num_pending_{};
    read_response_stream_fsm fsm_;

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
