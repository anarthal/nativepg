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

#include <cstddef>
#include <deque>
#include <optional>
#include <span>
#include <system_error>
#include <vector>

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
        for (std::size_t i = pending_offset_; i < elems_.size(); ++i)
        {
            auto& elm = elems_[i];
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
        pending_offset_ = elems_.size();

        return write_buffer_;
    }

    [[nodiscard]]
    std::error_code on_message(const protocol::any_backend_message& msg)
    {
        // TODO: handle cancellation
        using protocol::detail::read_response_fsm_impl;
        if (!fsm_.has_value())
        {
            // We're starting a new message. Ignore any abandoned
            // requests that are not expecting any message back
            while (elems_.front().abandoned_before_write)
            {
                elems_.pop_front();
                --pending_offset_;
            }

            // Start the new message
            fsm_.emplace(elems_.front().req, elems_.front().res);
        }

        // Handle the message
        auto res = fsm_->resume(msg);

        // If the FSM terminates, it means we're done with this request
        if (res.type == read_response_fsm_impl::result_type::done)
        {
            elems_.front().on_done(res.ec);
            elems_.pop_front();
            --pending_offset_;
            fsm_ = {};
        }

        // Any errors here are protocol violations and should cause connection teardown
        return res.ec;
    }

    // To be called when connection is lost.
    // Cancels requests that are in flight (i.e. not pending)
    void cancel_in_flight()
    {
        for (std::size_t i = 0u; i < pending_offset_; ++i)
            elems_[i].on_done(std::make_error_code(std::errc::operation_canceled));
        elems_.erase(elems_.begin(), elems_.begin() + pending_offset_);
        pending_offset_ = 0u;
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
    std::size_t pending_offset_{};

    // TODO: I don't like this
    std::optional<protocol::detail::read_response_fsm_impl> fsm_;

    inline static void ignore(std::error_code) {}
};

}  // namespace detail
}  // namespace nativepg

#endif  // BOOST_REDIS_MULTIPLEXER_HPP
