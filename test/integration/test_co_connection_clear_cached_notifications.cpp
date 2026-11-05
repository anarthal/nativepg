//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/capy/ex/async_event.hpp>
#include <boost/capy/ex/this_coro.hpp>
#include <boost/capy/results.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/when_all.hpp>
#include <boost/core/lightweight_test.hpp>

#include "nativepg/co_connection.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/notification_vector.hpp"
#include "nativepg/protocol/async.hpp"
#include "test_utils/ci_server.hpp"
#include "test_utils/co_connection_utils.hpp"
#include "test_utils/corosio_utils.hpp"
#include "test_utils/printing.hpp"
#include "test_utils/test_range_eq.hpp"
#include "test_utils/yield.hpp"

namespace capy = boost::capy;
using namespace nativepg;
using namespace nativepg::test;

namespace {

// There are notifications to discard when the function is called
capy::task<> test_with_cached_notifications()
{
    // Setup
    auto conn = co_await establish_connection();
    if (!co_await checked_exec(conn, "LISTEN test_clear_cached"))
        co_return;

    // Cache a notification that must never be delivered
    if (!co_await cache_notification(conn, "test_clear_cached", "discarded"))
        co_return;

    // Drop it
    conn.clear_cached_notifications();

    // Raise a second one, which should be the only thing receive() sees
    if (!co_await cache_notification(conn, "test_clear_cached", "delivered"))
        co_return;

    notification_vector notifs;
    if (!check_success(co_await conn.receive(notifs)))
        co_return;

    const protocol::notification_response expected[] = {
        {.process_id = conn.state().backend_process_id,
         .channel_name = "test_clear_cached",
         .payload = "delivered"}
    };
    test_range_eq(notifs, expected);

    // The connection is left in a usable state
    co_await check_connection_usable(conn);
}

// Clearing an empty cache is a no-op, and doesn't disturb later notifications
capy::task<> test_without_cached_notifications()
{
    // Setup
    auto conn = co_await establish_connection();
    if (!co_await checked_exec(conn, "LISTEN test_clear_empty"))
        co_return;

    // Nothing has been cached yet. This is a no-op
    conn.clear_cached_notifications();
    BOOST_TEST(conn.is_connected());

    // Notifications raised afterwards are delivered normally
    if (!co_await cache_notification(conn, "test_clear_empty", "after clear"))
        co_return;

    notification_vector notifs;
    if (!check_success(co_await conn.receive(notifs)))
        co_return;

    const protocol::notification_response expected[] = {
        {.process_id = conn.state().backend_process_id,
         .channel_name = "test_clear_empty",
         .payload = "after clear"}
    };
    test_range_eq(notifs, expected);

    // The connection is left in a usable state
    co_await check_connection_usable(conn);
}

// Safe on a connection that was never established
capy::task<> test_never_connected()
{
    // Setup
    co_connection conn{co_await capy::this_coro::executor};

    // This is a no-op
    conn.clear_cached_notifications();
    BOOST_TEST_NOT(conn.is_connected());

    // The connection can still be established and used
    diagnostics diag;
    if (!check_success(co_await conn.connect(default_connect_params(), &diag), diag))
        co_return;
    BOOST_TEST(conn.is_connected());
    co_await check_connection_usable(conn);
}

// Safe after a fatal error, with notifications from the dead session still cached
capy::task<> test_after_fatal_error()
{
    // Setup
    auto conn = co_await establish_connection();
    if (!co_await checked_exec(conn, "LISTEN test_clear_fatal"))
        co_return;

    // Cache a notification
    if (!co_await cache_notification(conn, "test_clear_fatal", "dead session"))
        co_return;

    // Break the connection
    if (!co_await break_connection(conn))
        co_return;

    // Clearing a dead connection's cache is safe
    conn.clear_cached_notifications();

    // And doesn't get in the way of recovering the connection
    diagnostics diag;
    if (!check_success(co_await conn.connect(default_connect_params(), &diag), diag))
        co_return;
    BOOST_TEST(conn.is_connected());
    co_await check_connection_usable(conn);
}

// Safe while a receive() is running
capy::task<> test_receive_in_progress()
{
    // Setup
    auto conn = co_await establish_connection(), notifier = co_await establish_connection();
    if (!co_await checked_exec(conn, "LISTEN test_clear_during_receive"))
        co_return;

    static_cast<void>(co_await capy::when_all(
        [&]() -> capy::task<capy::results<>> {
            // Receiver
            notification_vector notifs;
            if (!check_success(co_await conn.receive(notifs)))
                co_return {};
            const protocol::notification_response expected[] = {
                {.process_id = notifier.state().backend_process_id,
                 .channel_name = "test_clear_during_receive",
                 .payload = "after clear"}
            };
            test_range_eq(notifs, expected);
            co_return {};
        }(),

        [&]() -> capy::task<capy::results<>> {
            // Just in case. Ensure that the receiver is running
            co_await yield();

            // Clearing here is safe
            conn.clear_cached_notifications();
            conn.clear_cached_notifications();

            // Unblock the receiver
            co_await checked_exec(notifier, "NOTIFY test_clear_during_receive, 'after clear'");
            co_return {};
        }()
    ));

    // The connection is left in a usable state
    co_await check_connection_usable(conn);
}

}  // namespace

int main()
{
    run_coroutine_test(test_with_cached_notifications());
    run_coroutine_test(test_without_cached_notifications());
    run_coroutine_test(test_never_connected());
    run_coroutine_test(test_after_fatal_error());
    run_coroutine_test(test_receive_in_progress());

    return boost::report_errors();
}
