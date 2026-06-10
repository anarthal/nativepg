//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_NOTIFY_QUEUE_HPP
#define NATIVEPG_NOTIFY_QUEUE_HPP

#include <boost/assert.hpp>
#include <boost/capy/ex/async_event.hpp>
#include <boost/capy/io_task.hpp>

#include <cstddef>
#include <vector>

#include "nativepg/notify_event.hpp"
#include "nativepg/protocol/async.hpp"

namespace nativepg::detail {

// Handles notify events and backpressure in multiplexed connections
class notify_queue
{
    std::size_t max_pending_;
    std::vector<notify_event> pending_;
    boost::capy::async_event events_available_;
    boost::capy::async_event space_available_;
    // TODO: we could avoid copies if the consumer task is waiting

    bool has_space() const { return pending_.size() < max_pending_; }

    void add_event(notify_event&& evt)
    {
        pending_.push_back(std::move(evt));
        if (!has_space())
            space_available_.clear();
        events_available_.set();
    }

    void do_add_notification(const protocol::notification_response& msg)
    {
        add_event({
            .type = notify_event_type::notify,
            .backend_pid = msg.process_id,
            .channel = std::string(msg.channel_name),
            .payload = std::string(msg.payload),
        });
    }

public:
    notify_queue(std::size_t max_pending) : max_pending_(max_pending)
    {
        BOOST_ASSERT(max_pending > 0u);
        space_available_.set();
    }

    // Producer side. Connects and disconnects are not subject to backpressure
    // TODO: coalesce connect/disconnect events to avoid unlimited resource consumption
    void add_connect() { add_event({notify_event_type::connect}); }
    void add_disconnect() { add_event({notify_event_type::disconnect}); }

    // Notifications are subject to backpressure
    bool try_add_notify(const protocol::notification_response& msg)
    {
        if (!has_space())
            return false;
        do_add_notification(msg);
        return true;
    }

    boost::capy::io_task<> add_notify(const protocol::notification_response& msg)
    {
        while (!has_space())
        {
            if (auto [ec] = co_await space_available_.wait(); ec)
                co_return ec;
        }

        do_add_notification(msg);
        co_return {};
    }

    // Consumer side
    boost::capy::io_task<> read_events(std::vector<notify_event>& output)
    {
        // Wait for messages, if required
        while (pending_.empty())
        {
            if (auto [ec] = co_await events_available_.wait(); ec)
                co_return ec;
        }

        // Take the messages
        output.swap(pending_);
        pending_.clear();
        events_available_.clear();
        space_available_.set();

        // Done
        co_return {};
    }
};

}  // namespace nativepg::detail

#endif
