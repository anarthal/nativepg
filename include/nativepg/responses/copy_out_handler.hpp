//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_COPY_OUT_HANDLER_HPP
#define NATIVEPG_COPY_OUT_HANDLER_HPP

#include <cstddef>
#include <span>
#include <utility>

#include "nativepg/protocol/copy.hpp"
#include "nativepg/request.hpp"
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

// TODO: impl
template <copy_out_visitor Visitor>
class copy_out_handler_t
{
    Visitor cb_;
    command_info* info_{};

public:
    template <copy_out_visitor Cb>
    explicit copy_out_handler_t(Cb&& cb, command_info* out_info = nullptr)
        : cb_(std::forward<Cb>(cb)), info_(out_info)
    {
    }

    handler_setup_result setup(const request& req, std::size_t offset)
    {
        if (info_)
            detail::reset_info(*info_);
        return detail::resultset_setup(req, offset);
    }

    void on_message(const any_request_message& msg, std::size_t, extended_error& err);

    Visitor& get() { return cb_; }
    const Visitor& get() const { return cb_; }

    // {
    // using kind = any_request_message::kind;

    // switch (msg.type())
    // {
    //     // If the server sends an error, store it.
    //     // We know this is the last message in the sequence.
    //     case kind::error_response: detail::store_error(msg.get_error_response(), err); break;

    //     // Ignore messages that may or may not appear
    //     case kind::parse_complete:
    //     case kind::bind_complete: break;

    //     // Messages that we always expect
    //     case kind::row_description: on_row_description(msg.get_row_description(), err); break;
    //     case kind::data_row: on_data_row(msg.get_data_row(), err); break;
    //     case kind::command_complete: on_command_complete(msg.get_command_complete()); break;
    //     case kind::portal_suspended: on_portal_suspended(); break;

    //     // If any of the messages we expect was skipped due to a previous error,
    //     // that's an error
    //     case kind::message_skipped: err.code = client_errc::step_skipped; break;

    //     // We shouldn't get any unexpected messages
    //     default:
    //         err.code = client_errc::incompatible_response_type;  // just in case
    //         BOOST_ASSERT(false);
    //         break;
    // }
    // }
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
