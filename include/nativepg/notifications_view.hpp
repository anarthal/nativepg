//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_NOTIFICATIONS_VIEW_HPP
#define NATIVEPG_NOTIFICATIONS_VIEW_HPP

#include <span>

#include "nativepg/protocol/async.hpp"
#include "nativepg/responses/detail/dynamic_utils.hpp"  // TODO: move offset_and_length

namespace nativepg {

namespace detail {

struct offsetted_notification
{
    std::int32_t process_id{};
    offset_and_length channel_name{}, payload{};

    protocol::notification_response to_notification_response(const unsigned char* data) const
    {
        return {
            .process_id = process_id,
            .channel_name = channel_name.to_string_view(data),
            .payload = payload.to_string_view(data),
        };
    }
};

}  // namespace detail

class notifications_view
{
    std::span<const detail::offsetted_notification> elms_;
    const unsigned char* data_;

public:
    // value_type is protocol::notification_message
};

}  // namespace nativepg

#endif
