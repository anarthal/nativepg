//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_NOTIFY_EVENT_HPP
#define NATIVEPG_NOTIFY_EVENT_HPP

#include <cstdint>
#include <string>

namespace nativepg {

enum class notify_event_type
{
    notify,
    connect,
    disconnect,
};

// TODO: this should use a flat memory layout
// TODO: should be notification, not notify
struct notify_event
{
    // What did happen?
    notify_event_type type;

    // If type == notify, the process ID of the notifying backend
    std::int32_t backend_pid{};

    // If type == notify, the channel that was notified
    std::string channel{};

    // If type == notify, the payload passed to NOTIFY
    std::string payload{};
};

}  // namespace nativepg

#endif
