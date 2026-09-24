//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/capy/cond.hpp>
#include <boost/capy/ex/async_event.hpp>
#include <boost/capy/ex/immediate.hpp>
#include <boost/capy/ex/run.hpp>
#include <boost/capy/ex/this_coro.hpp>
#include <boost/capy/io_task.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/when_all.hpp>
#include <boost/core/lightweight_test.hpp>
#include <boost/describe/class.hpp>
#include <boost/describe/operators.hpp>

#include <cstddef>
#include <cstdint>
#include <ostream>
#include <stop_token>
#include <string_view>
#include <system_error>

#include "nativepg/client_errc.hpp"
#include "nativepg/co_connection.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/notification_vector.hpp"
#include "nativepg/protocol/async.hpp"
#include "nativepg/request.hpp"
#include "nativepg/responses/check.hpp"
#include "nativepg/responses/response.hpp"
#include "nativepg/responses/response_handler.hpp"
#include "nativepg/responses/resultset_callback.hpp"
#include "test_utils/co_connection_utils.hpp"
#include "test_utils/corosio_utils.hpp"
#include "test_utils/printing.hpp"
#include "test_utils/test_cond_eq.hpp"
#include "test_utils/test_range_eq.hpp"

namespace capy = boost::capy;
using namespace nativepg;
using namespace nativepg::test;
using namespace std::string_view_literals;
using namespace std::chrono_literals;

namespace nativepg::protocol {

std::ostream& operator<<(std::ostream& os, const notification_response& value)
{
    return os << "{ .process_id=" << value.process_id << ", .channel_name=" << value.channel_name
              << ", .payload=" << value.payload << " }";
}

}  // namespace nativepg::protocol

namespace {

struct row_int
{
    int value;
};
BOOST_DESCRIBE_STRUCT(row_int, (), (value))

using boost::describe::operators::operator==;
using boost::describe::operators::operator<<;

// A notification raised by another connection reaches a receive() that runs
// alone on an established connection
capy::task<> test_single_notification()
{
    // Setup
    auto conn = co_await establish_connection(), notifier = co_await establish_connection();

    // Listen
    if (!co_await checked_exec(conn, request().add_query("LISTEN \"test_receive_single\"")))
        co_return;

    // Raise the notification
    if (!co_await checked_exec(notifier, request().add_query("NOTIFY test_receive_single, 'some payload'")))
        co_return;

    // Receive
    notification_vector notifs;
    if (!check_success(co_await conn.receive(notifs)))
        co_return;

    // Check
    const protocol::notification_response expected[] = {
        {.process_id = notifier.state().backend_process_id,
         .channel_name = "test_receive_single",
         .payload = "some payload"}
    };
    test_range_eq(notifs, expected);

    // Raise a second notification
    if (!co_await checked_exec(notifier, request().add_query("NOTIFY test_receive_single, 'second payload'")))
        co_return;

    // The next receive() delivers only the new one: the previous batch was consumed
    if (!check_success(co_await conn.receive(notifs), {}))
        co_return;

    const protocol::notification_response expected2[] = {
        {.process_id = notifier.state().backend_process_id,
         .channel_name = "test_receive_single",
         .payload = "second payload"}
    };
    test_range_eq(notifs, expected2);

    // The connection is left in a usable state
    co_await check_connection_usable(conn);
}

// Empty payloads don't cause trouble
capy::task<> test_empty_payload()
{
    // Setup
    auto conn = co_await establish_connection(), notifier = co_await establish_connection();

    // Listen
    if (!co_await checked_exec(conn, request().add_query("LISTEN \"test_empty_payload\"")))
        co_return;

    // Raise the notification
    if (!co_await checked_exec(notifier, request().add_query("NOTIFY test_empty_payload")))
        co_return;

    // Receive
    notification_vector notifs;
    if (!check_success(co_await conn.receive(notifs)))
        co_return;

    // Check
    const protocol::notification_response expected[] = {
        {.process_id = notifier.state().backend_process_id,
         .channel_name = "test_empty_payload",
         .payload = ""}
    };
    test_range_eq(notifs, expected);

    // The connection is left in a usable state
    co_await check_connection_usable(conn);
}

// A notification that arrives while an exec() owns the reader is stored,
// rather than discarded, and delivered by the next receive()
capy::task<> test_notification_during_exec()
{
    // Setup
    auto conn = co_await establish_connection();

    // Listen
    if (!co_await checked_exec(conn, request().add_query("LISTEN \"test_during_exec\"")))
        co_return;

    // Raise the notification. Make sure it arrives during exec
    request req_notify;
    req_notify.add_query("NOTIFY test_during_exec, 'during exec'");
    req_notify.add_query("SELECT 1");
    if (!co_await checked_exec(conn, req_notify))
        co_return;

    // Receive
    notification_vector notifs;
    if (!check_success(co_await conn.receive(notifs)))
        co_return;

    // Check
    const protocol::notification_response expected[] = {
        {.process_id = conn.state().backend_process_id,
         .channel_name = "test_during_exec",
         .payload = "during exec"}
    };
    test_range_eq(notifs, expected);

    // The connection is left in a usable state
    co_await check_connection_usable(conn);
}

// The view returned by receive() stays valid when an exec() runs afterwards,
// even if that exec() reads further notifications
capy::task<> test_exec_doesnt_invalidate_notifications()
{
    // Setup
    auto conn = co_await establish_connection();

    // Listen
    if (!co_await checked_exec(conn, request().add_query("LISTEN \"test_no_invalidate\"")))
        co_return;

    // Raise the notification. Make sure it arrives during exec
    request req_notify;
    req_notify.add_query("NOTIFY test_no_invalidate, 'first notification payload'");
    req_notify.add_query("SELECT 1");
    if (!co_await checked_exec(conn, req_notify))
        co_return;

    // Retrieve it
    notification_vector notifs;
    if (!check_success(co_await conn.receive(notifs)))
        co_return;
    const protocol::notification_response expected[] = {
        {.process_id = conn.state().backend_process_id,
         .channel_name = "test_no_invalidate",
         .payload = "first notification payload"}
    };
    test_range_eq(notifs, expected);
    if (!BOOST_TEST_EQ(notifs.size(), static_cast<std::size_t>(1)))
        co_return;

    // A second notification arrives while an exec() is reading, so the connection
    // has to buffer it without disturbing what we were handed above
    req_notify = {};
    req_notify.add_query("NOTIFY test_no_invalidate, 'second payload here'");
    req_notify.add_query("SELECT 2");
    if (!co_await checked_exec(conn, req_notify))
        co_return;

    // Check that the view we got before the exec() still reads correctly
    test_range_eq(notifs, expected);

    // The connection is left in a usable state
    co_await check_connection_usable(conn);
}

// Notifications raised by a single transaction are delivered as a single batch
capy::task<> test_batch_notifications()
{
    // Setup
    auto conn = co_await establish_connection(), notifier = co_await establish_connection();

    // Listen
    if (!co_await checked_exec(conn, request().add_query("LISTEN \"test_batch\"")))
        co_return;

    // Raise the notifications. Payloads must differ because Postgres deduplicates
    request req_notify{false};
    req_notify.add_query("NOTIFY test_batch, 'first'");
    req_notify.add_query("NOTIFY test_batch, 'second'");
    req_notify.add_sync();
    if (!co_await checked_exec(notifier, req_notify))
        co_return;

    // Receive
    notification_vector notifs;
    if (!check_success(co_await conn.receive(notifs)))
        co_return;

    // Check
    const auto notifier_pid = notifier.state().backend_process_id;
    const protocol::notification_response expected[] = {
        {.process_id = notifier_pid, .channel_name = "test_batch", .payload = "first" },
        {.process_id = notifier_pid, .channel_name = "test_batch", .payload = "second"},
    };
    test_range_eq(notifs, expected);

    // The connection is left in a usable state
    co_await check_connection_usable(conn);
}

//
// Synchronization between exec() and receive()
//

// A receive() started before anything else has run on the connection, including
// the LISTEN. There is no exec() in flight to hand the reader over to it
capy::task<> test_receive_before_any_exec()
{
    // Setup
    auto conn = co_await establish_connection(), notifier = co_await establish_connection();

    // Recall that when_all launches things in order, so the receive() below starts
    // on a connection where no request has run yet
    static_cast<void>(co_await capy::when_all(
        [&]() -> capy::io_task<> {
            // Takes the reader on an idle connection, then has to hand it over to
            // the LISTEN below and wait until the notification arrives
            notification_vector notifs;
            if (check_success(co_await conn.receive(notifs)))
            {
                const protocol::notification_response expected[] = {
                    {.process_id = notifier.state().backend_process_id,
                     .channel_name = "test_before_any_exec",
                     .payload = "no previous exec"}
                };
                test_range_eq(notifs, expected);
            }
            co_return {};
        }(),

        [&]() -> capy::io_task<> {
            // Subscribe only now, with the receive() already in flight
            co_await checked_exec(conn, request().add_query("LISTEN \"test_before_any_exec\""));

            // Unblock receive
            co_await checked_exec(
                notifier,
                request().add_query("NOTIFY test_before_any_exec, 'no previous exec'")
            );
            co_return {};
        }()
    ));

    // The connection is left in a usable state
    co_await check_connection_usable(conn);
}

// A receive() issued while an exec() already owns the reader waits for its turn.
// exec() hands over to receive() correctly
capy::task<> test_receive_during_exec_handover()
{
    // Setup
    auto conn = co_await establish_connection(), notifier = co_await establish_connection(),
         locker = co_await establish_connection();

    // Acquire the lock
    constexpr std::int64_t lock_id = 10;
    if (!co_await checked_exec(locker, request().add_query("SELECT pg_advisory_lock($1)", lock_id)))
        co_return;

    // Listen
    if (!co_await checked_exec(conn, request().add_query("LISTEN \"test_receive_during_exec\"")))
        co_return;

    // Blocks on the lock, keeping the reader busy
    request req_exec;
    req_exec.add_query("SELECT $1 AS value", 42);
    req_exec.add_query("SELECT pg_advisory_lock($1)", lock_id);

    capy::async_event select_finished, exec_finished;

    // Recall that when_all launches things in order
    static_cast<void>(co_await capy::when_all(
        [&]() -> capy::io_task<> {
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

        [&]() -> capy::io_task<> {
            // Wait for exec to be in progress
            check_success(co_await select_finished.wait());

            // Retrieve the notifications
            notification_vector notifs;
            if (check_success(co_await conn.receive(notifs)))
            {
                const protocol::notification_response expected[] = {
                    {.process_id = notifier.state().backend_process_id,
                     .channel_name = "test_receive_during_exec",
                     .payload = "my payload"}
                };
                test_range_eq(notifs, expected);
            }
            co_return {};
        }(),

        [&]() -> capy::io_task<> {
            // Once exec is blocked on the lock, unlock, so things make progress
            check_success(co_await select_finished.wait());
            check_success(co_await locker.shutdown());

            // Once exec has finished, issue a notification to unblock the receiver
            check_success(co_await exec_finished.wait());
            co_await checked_exec(
                notifier,
                request().add_query("NOTIFY test_receive_during_exec, 'my payload'")
            );

            co_return {};
        }()
    ));

    // The connection is left in a usable state
    co_await check_connection_usable(conn);
}

// A receive() issued while an exec() already owns the reader waits for its turn,
// but is woken if exec() receives a notification
capy::task<> test_receive_during_exec_gets_notifications()
{
    // Setup
    auto conn = co_await establish_connection(), locker = co_await establish_connection();

    // Acquire the lock
    constexpr std::int64_t lock_id = 11;
    if (!co_await checked_exec(locker, request().add_query("SELECT pg_advisory_lock($1)", lock_id)))
        co_return;

    // Listen
    if (!co_await checked_exec(conn, request().add_query("LISTEN test_receive_during_exec_notif")))
        co_return;

    // Issues a query to give time to receive() to start and wait,
    // then notifies and blocks, to check that receive() completed before the exec().
    // Receive may actually start before the exec() starts reading, but would
    // eventually yield to exec and hit the case we want
    request req_exec;
    req_exec.add_query("SELECT 42");  // give time to
    req_exec.add_query("NOTIFY test_receive_during_exec_notif, 'my payload'");
    req_exec.add_query("SELECT pg_advisory_lock($1)", lock_id);

    // Recall that when_all launches things in order
    static_cast<void>(co_await capy::when_all(
        [&]() -> capy::io_task<> {
            co_await checked_exec(conn, req_exec);
            co_return {};
        }(),

        [&]() -> capy::io_task<> {
            // Retrieve the notifications
            notification_vector notifs;
            if (check_success(co_await conn.receive(notifs)))
            {
                const protocol::notification_response expected[] = {
                    {.process_id = conn.state().backend_process_id,
                     .channel_name = "test_receive_during_exec_notif",
                     .payload = "my payload"}
                };
                test_range_eq(notifs, expected);
            }

            // Unblock exec
            check_success(co_await locker.shutdown());
            co_return {};
        }()
    ));

    // The connection is left in a usable state
    co_await check_connection_usable(conn);
}

// An exec() started while a receive() owns the reader. The exec's response
// messages reach the receiver, which has to hand the reader over rather than
// consume them
capy::task<> test_exec_starts_during_receive()
{
    // Setup
    auto conn = co_await establish_connection();

    // Listen
    if (!co_await checked_exec(conn, request().add_query("LISTEN \"test_exec_during_receive\"")))
        co_return;

    // Blocks on the lock, so the exec stays in flight while we set things up
    request req_exec;
    req_exec.add_query("SELECT $1 AS value", 42);
    req_exec.add_query("SELECT pg_advisory_lock($1)", 12);

    // when_all launches things in order, so the receive() runs first and takes
    // the reader before the exec below registers itself
    static_cast<void>(co_await capy::when_all(
        [&]() -> capy::io_task<> {
            // Owns the reader until the exec's first response message arrives
            notification_vector notifs;
            if (check_success(co_await conn.receive(notifs)))
            {
                const protocol::notification_response expected[] = {
                    {.process_id = conn.state().backend_process_id,
                     .channel_name = "test_exec_during_receive",
                     .payload = "my payload"}
                };
                test_range_eq(notifs, expected);
            }
            co_return {};
        }(),

        [&]() -> capy::io_task<> {
            // exec() should't be blocked by the ongoing receive().
            // If it is, we don't raise the notification in the following
            // line, and the test fails by timeout.
            co_await checked_exec(conn, request().add_query("SELECT 42"));

            // Unblock the receiver to finish the test
            co_await checked_exec(conn, request().add_query("NOTIFY test_exec_during_receive, 'my payload'"));

            co_return {};
        }()
    ));

    // The connection is left in a usable state
    co_await check_connection_usable(conn);
}

//
// Cancellation
//

// A receive() cancelled as soon as it starts doesn't leave the cancellation
// behind as a pending error for the next one
capy::task<> test_cancel_immediately()
{
    // Setup
    auto conn = co_await establish_connection(), notifier = co_await establish_connection();

    // Listen
    if (!co_await checked_exec(conn, request().add_query("LISTEN \"test_cancel_immediate\"")))
        co_return;

    // Run a receive() that gets cancelled immediately
    static_cast<void>(co_await capy::when_all(
        [&]() -> capy::io_task<> {
            notification_vector notifs;
            auto [ec] = co_await conn.receive(notifs);
            test_cond_eq(ec, capy::cond::canceled);
            co_return {};
        }(),
        capy::ready(std::make_error_code(std::errc::io_error))
    ));

    // Raise a notification
    if (!co_await checked_exec(notifier, request().add_query("NOTIFY test_cancel_immediate, 'after cancel'")))
        co_return;

    // The next receive() succeeds: the cancellation wasn't stored
    notification_vector notifs;
    if (check_success(co_await conn.receive(notifs)))
    {
        const protocol::notification_response expected[] = {
            {.process_id = notifier.state().backend_process_id,
             .channel_name = "test_cancel_immediate",
             .payload = "after cancel"}
        };
        test_range_eq(notifs, expected);
    }

    // The connection is left in a usable state
    co_await check_connection_usable(conn);
}

// A receive() skips the messages that the server still owes for a cancelled
// exec() before reporting notifications
capy::task<> test_receive_reads_exec_leftovers()
{
    // Setup
    auto conn = co_await establish_connection(), notifier = co_await establish_connection(),
         locker = co_await establish_connection();

    // Acquire the lock
    constexpr std::int64_t lock_id = 13;
    if (!co_await checked_exec(locker, request().add_query("SELECT pg_advisory_lock($1)", lock_id)))
        co_return;

    // Listen
    if (!co_await checked_exec(conn, request().add_query("LISTEN test_receive_exec_leftovers")))
        co_return;

    // Blocks on the lock. Cancelling it with part of its response already read
    // leaves the rest owed by the server
    request req_exec;
    req_exec.add_query("SELECT $1 AS value", 42);
    req_exec.add_query("SELECT pg_advisory_lock($1)", lock_id);

    capy::async_event select_finished, exec_finished;
    std::stop_source stop_src;

    // Recall that when_all launches things in order
    static_cast<void>(co_await capy::when_all(
        capy::run(stop_src.get_token())([&]() -> capy::io_task<> {
            // Cancelled while blocked on the lock
            row_int row{};
            auto cb = [&](row_int r) {
                row = r;
                select_finished.set();
            };
            response resp{resultset_callback<row_int>(cb), check_execute()};
            auto [ec] = co_await conn.exec(req_exec, &resp);
            test_cond_eq(ec, capy::cond::canceled);
            BOOST_TEST_EQ(row, row_int{.value = 42});
            exec_finished.set();
            co_return {};
        }()),

        [&]() -> capy::io_task<> {
            // Becomes the reader once the exec is gone, and has to consume what
            // the server still owes for it before it can see the notification
            notification_vector notifs;
            if (check_success(co_await conn.receive(notifs)))
            {
                const protocol::notification_response expected[] = {
                    {.process_id = notifier.state().backend_process_id,
                     .channel_name = "test_receive_exec_leftovers",
                     .payload = "after leftovers"}
                };
                test_range_eq(notifs, expected);
            }
            co_return {};
        }(),

        [&]() -> capy::io_task<> {
            // Cancel the exec once part of its response has been read
            check_success(co_await select_finished.wait());
            stop_src.request_stop();

            // Release the lock once exec() finished
            check_success(co_await exec_finished.wait());
            check_success(co_await locker.shutdown());

            // Raise a notification, so the receive() has something to report
            co_await checked_exec(
                notifier,
                request().add_query("NOTIFY test_receive_exec_leftovers, 'after leftovers'")
            );
            co_return {};
        }()
    ));

    // The connection is left in a usable state
    co_await check_connection_usable(conn);
}

// Starting a receive() while another one is in progress is rejected,
// without disturbing the one that is running
capy::task<> test_receive_already_running()
{
    // Setup
    auto conn = co_await establish_connection(), notifier = co_await establish_connection();

    // Listen
    if (!co_await checked_exec(conn, request().add_query("LISTEN \"test_already_running\"")))
        co_return;

    // Recall that when_all launches things in order
    static_cast<void>(co_await capy::when_all(
        [&]() -> capy::io_task<> {
            // Takes the receiver slot and waits for notifications
            notification_vector notifs;
            if (check_success(co_await conn.receive(notifs)))
            {
                const protocol::notification_response expected[] = {
                    {.process_id = notifier.state().backend_process_id,
                     .channel_name = "test_already_running",
                     .payload = "only receiver"}
                };
                test_range_eq(notifs, expected);
            }
            co_return {};
        }(),

        [&]() -> capy::io_task<> {
            // The slot is taken, so this one fails without touching the connection
            notification_vector notifs;
            auto [ec] = co_await conn.receive(notifs);
            BOOST_TEST_EQ(ec, make_error_code(client_errc::already_running));

            // Unblock the receiver that is running
            co_await checked_exec(
                notifier,
                request().add_query("NOTIFY test_already_running, 'only receiver'")
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
    run_coroutine_test(test_single_notification());
    run_coroutine_test(test_empty_payload());
    run_coroutine_test(test_notification_during_exec());
    run_coroutine_test(test_exec_doesnt_invalidate_notifications());
    run_coroutine_test(test_batch_notifications());

    run_coroutine_test(test_receive_before_any_exec());
    run_coroutine_test(test_receive_during_exec_handover());
    run_coroutine_test(test_receive_during_exec_gets_notifications());
    run_coroutine_test(test_exec_starts_during_receive());

    run_coroutine_test(test_cancel_immediately());
    run_coroutine_test(test_receive_reads_exec_leftovers());
    run_coroutine_test(test_receive_already_running());

    return boost::report_errors();
}
