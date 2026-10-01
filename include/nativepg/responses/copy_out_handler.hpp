//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_COPY_OUT_HANDLER_HPP
#define NATIVEPG_COPY_OUT_HANDLER_HPP

#include <boost/assert.hpp>

#include <cstddef>
#include <span>
#include <utility>

#include "nativepg/client_errc.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/protocol/copy.hpp"
#include "nativepg/request.hpp"
#include "nativepg/responses/any_request_message.hpp"
#include "nativepg/responses/command_info.hpp"
#include "nativepg/responses/detail/response_utils.hpp"
#include "nativepg/responses/response_handler.hpp"

namespace nativepg {

template <class T>
concept copy_out_visitor = requires(
    T& obj,
    const protocol::copy_out_response& copy_out,
    const protocol::copy_data& copy_data
) {
    obj.on_copy_start(copy_out);
    obj.on_copy_data(copy_data);
};

template <class T>
concept copy_out_callback = requires(T& obj, std::span<const unsigned char> copy_data) { obj(copy_data); };

// Handles a single COPY OUT operation by invoking a user-supplied visitor
// TODO: erase this
// TODO: unit test
template <copy_out_visitor Visitor>
class copy_out_handler_t
{
    enum class state_t
    {
        // We haven't seen the copy_out_response yet
        initial,

        // We got the copy_out_response and are receiving data
        copying,

        // We got the command_complete that terminates the copy
        done,

        // Something went wrong. Ignore any further messages
        failed,
    };

    state_t state_{state_t::initial};
    Visitor cb_;
    command_info* info_{};

    // Records an error and stops processing further messages.
    // The FSM makes the first error win, so this never overwrites a previous one
    void fail(std::error_code ec, extended_error& out_err)
    {
        out_err.code = ec;
        state_ = state_t::failed;
    }

public:
    template <copy_out_visitor Cb>
    explicit copy_out_handler_t(Cb&& cb, command_info* out_info = nullptr)
        : cb_(std::forward<Cb>(cb)), info_(out_info)
    {
    }

    handler_setup_result setup(const request& req, std::size_t offset)
    {
        state_ = state_t::initial;
        if (info_)
            detail::reset_info(*info_);
        return detail::resultset_setup(req, offset);
    }

    void on_message(const any_request_message& msg, std::size_t, extended_error& err)
    {
        using kind = any_request_message::kind;

        // Once we've failed, we don't care about anything else
        if (state_ == state_t::failed)
            return;

        switch (msg.type())
        {
            // If the server sends an error, store it.
            // We know this is the last message in the sequence.
            case kind::error_response:
                detail::store_error(msg.get_error_response(), err);
                state_ = state_t::failed;
                break;

            // Ignore messages that may or may not appear.
            // COPY statements produce no rows, so the describe step yields an empty row_description
            case kind::parse_complete:
            case kind::bind_complete:
            case kind::row_description: break;

            // Starts the copy. Exactly one of these is expected
            case kind::copy_out_response:
                if (state_ != state_t::initial)
                    fail(client_errc::incompatible_response_type, err);
                else
                {
                    state_ = state_t::copying;
                    cb_.on_copy_start(msg.get_copy_out_response());
                }
                break;

            // Data for the copy we started. The FSM only emits these while copying
            case kind::copy_data:
                BOOST_ASSERT(state_ == state_t::copying);
                cb_.on_copy_data(msg.get_copy_data());
                break;

            // Terminates the copy. If we never saw a copy_out_response,
            // the request wasn't a COPY ... TO STDOUT
            case kind::command_complete:
                if (state_ != state_t::copying)
                    fail(client_errc::incompatible_response_type, err);
                else
                {
                    if (info_)
                        detail::from_command_complete(*info_, msg.get_command_complete());
                    state_ = state_t::done;
                }
                break;

            // If any of the messages we expect was skipped due to a previous error,
            // that's an error
            case kind::message_skipped: fail(client_errc::step_skipped, err); break;

            // Anything else means the request didn't contain a single COPY ... TO STDOUT.
            // This is reachable by pairing this handler with the wrong statement,
            // so it's a user error rather than a protocol violation
            default: fail(client_errc::incompatible_response_type, err); break;
        }
    }
};

namespace detail {

template <copy_out_callback Callback>
struct copy_out_visitor_adapter
{
    Callback cb;

    void on_copy_start(const protocol::copy_out_response&) {}
    void on_copy_data(const protocol::copy_data& data) { cb(data.data); }
};

}  // namespace detail

// TODO: do we need this?
// TODO: decaying
template <copy_out_visitor Visitor>
copy_out_handler_t<Visitor> copy_out_handler(Visitor&& cb, command_info* info = nullptr)
{
    return copy_out_handler_t<Visitor>(std::forward<Visitor>(cb), info);
}

template <copy_out_callback Callback>
auto copy_out_handler(Callback&& cb, command_info* info = nullptr)
{
    return copy_out_handler_t<detail::copy_out_visitor_adapter<Callback>>(
        detail::copy_out_visitor_adapter<Callback>{std::forward<Callback>(cb)},
        info
    );
}

}  // namespace nativepg

#endif
