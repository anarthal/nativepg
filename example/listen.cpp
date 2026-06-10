//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/capy/ex/run_async.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/when_any.hpp>
#include <boost/corosio/io_context.hpp>
#include <boost/describe/class.hpp>

#include <algorithm>
#include <iostream>
#include <span>
#include <vector>

#include "nativepg/co_multiplexed_connection.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/notification_event.hpp"
#include "nativepg/request.hpp"

using namespace nativepg;
namespace capy = boost::capy;
namespace corosio = boost::corosio;

// Should we attempt to issue another LISTEN?
// Every time we connect we should renew our listens.
// Issuing the same listen several times is OK, so finding connect events is enough
// TODO: we should have an automatic re-subscription feature
static bool should_issue_listen(std::span<const notification_event> events)
{
    return std::ranges::find_if(events, [](const notification_event& evt) {
               return evt.type == notification_event_type::connect;
           }) != events.end();
}

static void print_event(const notification_event& event)
{
    switch (event.type)
    {
        case nativepg::notification_event_type::connect: std::cout << "Connection established\n"; break;
        case nativepg::notification_event_type::disconnect: std::cout << "Connection disconnected\n"; break;
        case nativepg::notification_event_type::notify:
            std::cout << "Received notification from process " << event.backend_pid << ", channel '"
                      << event.channel << "', payload '" << event.payload << "'\n";
            break;
    }
}

// TODO: replace this by the ignore response once we have it
class null_handler
{
    extended_error err_;

public:
    null_handler() = default;
    handler_setup_result setup(const request& req, std::size_t) { return req.messages().size(); }
    void on_message(const any_request_message&, std::size_t) {}
    const extended_error& result() const { return err_; }
};

static capy::io_task<> listener(co_multiplexed_connection& conn)
{
    std::vector<notification_event> events;

    request req;
    req.add_simple_query("LISTEN mychannel");

    null_handler h;

    while (true)
    {
        // Wait for new events
        if (auto [ec] = co_await conn.read_notifications(events); ec)
        {
            std::cerr << "Error reading events: " << ec << ": " << ec.message() << std::endl;
            co_return {};
        }

        // Print notifications
        for (const auto& event : events)
            print_event(event);

        // If there was a reconnection, listen again, since
        // reconnections remove listeners
        if (should_issue_listen(events))
        {
            if (auto [ec] = co_await conn.exec(req, h); ec)
            {
                std::cerr << "Error issuing listening: " << ec << ": " << ec.message() << std::endl;
                co_return {};
            }
        }
    }
}

static capy::task<> co_main()
{
    // Create a connection
    co_multiplexed_connection conn{co_await capy::this_coro::executor};

    // clang-format off
    multiplexed_config cfg{
        .transport = {
            .hostname = "localhost",
            .username = "postgres",
            .password = "secret",
            .database = "postgres",
        }
    };
    // clang-format on

    // Listen for notifications and run the connection so notifications are delivered
    co_await capy::when_any(conn.run(std::move(cfg)), listener(conn));
}

int main()
{
    // The I/O context, required for all I/O operations
    corosio::io_context ctx;

    // Schedules the main coroutine for execution
    capy::run_async(
        ctx.get_executor(),
        []() {
           // Runs when the main coroutine finishes normally
           std::cout << "Done\n";
        },
        [](std::exception_ptr exc) {
            // Runs when the main coroutine finishes with an exception
            try {
               std::rethrow_exception(exc);
            } catch (const std::exception& e) {
               std::cerr << "Error: " << e.what() << std::endl;
            }
            exit(1);
        }
    )(co_main());

    // Executes all pending work, including the main coroutine
    ctx.run();
}
