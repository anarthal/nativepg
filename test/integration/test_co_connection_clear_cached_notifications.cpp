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
#include <boost/describe/class.hpp>
#include <boost/describe/operators.hpp>

#include <cstdint>
#include <system_error>

#include "nativepg/co_connection.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/notification_vector.hpp"
#include "nativepg/protocol/async.hpp"
#include "nativepg/request.hpp"
#include "nativepg/responses/check.hpp"
#include "nativepg/responses/response.hpp"
#include "nativepg/responses/resultset_callback.hpp"
#include "test_utils/ci_server.hpp"
#include "test_utils/co_connection_utils.hpp"
#include "test_utils/corosio_utils.hpp"
#include "test_utils/printing.hpp"
#include "test_utils/test_range_eq.hpp"

namespace capy = boost::capy;
using namespace nativepg;
using namespace nativepg::test;

namespace {

struct row_int
{
    int value;
};
BOOST_DESCRIBE_STRUCT(row_int, (), (value))

using boost::describe::operators::operator==;
using boost::describe::operators::operator<<;

// Raises a notification on conn's own session and makes sure it has been read
// into the cache by the time we return. The trailing SELECT forces the reader
// to go past the NotificationResponse
capy::task<bool> cache_notification(co_connection& conn, std::string_view channel, std::string_view payload)
{
    request req;
    req.add_query("NOTIFY " + std::string(channel) + ", '" + std::string(payload) + "'");
    req.add_query("SELECT 1");
    co_return co_await checked_exec(conn, req);
}

// Notifications sitting in the cache are discarded, and don't reach a later receive()
capy::task<> test_with_cached_notifications()
{
    // Setup
    auto conn = co_await establish_connection();
    if (!co_await checked_exec(conn, "LISTEN \"test_clear_cached\""))
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
    if (!co_await checked_exec(conn, "LISTEN \"test_clear_empty\""))
        co_return;

    // Nothing has been cached yet. Clearing twice is still a no-op
    conn.clear_cached_notifications();
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

// Safe on a connection that was never established, and doesn't prevent
// connecting afterwards
capy::task<> test_never_connected()
{
    // Setup: a connection that was never established
    co_connection conn{co_await capy::this_coro::executor};

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
    if (!co_await checked_exec(conn, "LISTEN \"test_clear_fatal\""))
        co_return;

    // Cache a notification, so the clear below has something to do
    if (!co_await cache_notification(conn, "test_clear_fatal", "dead session"))
        co_return;

    // Ask the server to kill the backend serving us. It answers with a FATAL
    // error and closes the connection
    request req_kill;
    req_kill.add_query("SELECT pg_terminate_backend(pg_backend_pid())");
    auto [ec] = co_await conn.exec(req_kill, check());
    BOOST_TEST_NE(ec, std::error_code());
    if (!BOOST_TEST_NOT(conn.is_connected()))
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

// Safe while a receive() owns the reader and is waiting on the socket
capy::task<> test_receive_in_progress()
{
    // Setup
    auto conn = co_await establish_connection(), notifier = co_await establish_connection();
    if (!co_await checked_exec(conn, "LISTEN \"test_clear_during_receive\""))
        co_return;

    // Recall that when_all launches things in order, so the receive() below is
    // already suspended waiting for notifications when the clear runs
    static_cast<void>(co_await capy::when_all(
        [&]() -> capy::task<capy::results<>> {
            notification_vector notifs;
            if (check_success(co_await conn.receive(notifs)))
            {
                const protocol::notification_response expected[] = {
                    {.process_id = notifier.state().backend_process_id,
                     .channel_name = "test_clear_during_receive",
                     .payload = "after clear"}
                };
                test_range_eq(notifs, expected);
            }
            co_return {};
        }(),

        [&]() -> capy::task<capy::results<>> {
            // The receiver is waiting on the socket. Clearing must neither
            // disturb it nor lose its wakeup
            conn.clear_cached_notifications();
            conn.clear_cached_notifications();

            // Unblock the receiver
            co_await checked_exec(
                notifier,
                request().add_query("NOTIFY test_clear_during_receive, 'after clear'")
            );
            co_return {};
        }()
    ));

    // The connection is left in a usable state
    co_await check_connection_usable(conn);
}

// Safe while a receive() is waiting for its turn, with an exec() owning the
// reader. This is a different suspension point from the test above: the
// receiver is parked on the multiplexer's receive event, not on the socket
capy::task<> test_receive_waiting_for_turn()
{
    // Setup
    auto conn = co_await establish_connection(), notifier = co_await establish_connection(),
         locker = co_await establish_connection();

    // Acquire the lock
    constexpr std::int64_t lock_id = 30;
    if (!co_await checked_exec(locker, request().add_query("SELECT pg_advisory_lock($1)", lock_id)))
        co_return;

    // Listen
    if (!co_await checked_exec(conn, "LISTEN \"test_clear_waiting_turn\""))
        co_return;

    // The first query tells us the exec owns the reader; the second blocks on
    // the lock, keeping it there while we clear
    request req_exec;
    req_exec.add_query("SELECT $1 AS value", 42);
    req_exec.add_query("SELECT pg_advisory_lock($1)", lock_id);

    capy::async_event select_finished, exec_finished;

    // Recall that when_all launches things in order
    static_cast<void>(co_await capy::when_all(
        [&]() -> capy::task<capy::results<>> {
            // Owns the reader until the lock is released
            row_int row{};
            auto cb = [&](row_int r) {
                row = r;
                select_finished.set();
            };
            response resp{resultset_callback<row_int>(cb), check_execute()};
            if (co_await checked_exec(conn, req_exec, &resp))
                BOOST_TEST_EQ(row, row_int{.value = 42});
            exec_finished.set();
            co_return {};
        }(),

        [&]() -> capy::task<capy::results<>> {
            // Yields the reader to the exec above, then parks on the receive event
            notification_vector notifs;
            if (check_success(co_await conn.receive(notifs)))
            {
                const protocol::notification_response expected[] = {
                    {.process_id = notifier.state().backend_process_id,
                     .channel_name = "test_clear_waiting_turn",
                     .payload = "after clear"}
                };
                test_range_eq(notifs, expected);
            }
            co_return {};
        }(),

        [&]() -> capy::task<capy::results<>> {
            // Once the exec is reading, the receiver is parked waiting for its turn
            check_success(co_await select_finished.wait());
            conn.clear_cached_notifications();

            // Let the exec finish
            check_success(co_await locker.shutdown());
            check_success(co_await exec_finished.wait());

            // Unblock the receiver
            co_await checked_exec(
                notifier,
                request().add_query("NOTIFY test_clear_waiting_turn, 'after clear'")
            );
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
    run_coroutine_test(test_receive_waiting_for_turn());

    return boost::report_errors();
}
