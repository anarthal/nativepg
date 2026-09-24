//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/capy/ex/async_event.hpp>
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
#include <string_view>
#include <system_error>
#include <utility>

#include "nativepg/co_connection.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/protocol/async.hpp"
#include "nativepg/request.hpp"
#include "nativepg/responses/check.hpp"
#include "nativepg/responses/response.hpp"
#include "nativepg/responses/response_handler.hpp"
#include "nativepg/responses/resultset_callback.hpp"
#include "test_utils/co_connection_utils.hpp"
#include "test_utils/corosio_utils.hpp"
#include "test_utils/printing.hpp"
#include "test_utils/test_range_eq.hpp"

namespace capy = boost::capy;
using namespace nativepg;
using namespace nativepg::test;
using namespace std::string_view_literals;

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
    auto [ec, notifs] = co_await conn.receive();
    if (!check_success(ec, {}))
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
    auto [ec2, notifs2] = co_await conn.receive();
    if (!check_success(ec2, {}))
        co_return;

    const protocol::notification_response expected2[] = {
        {.process_id = notifier.state().backend_process_id,
         .channel_name = "test_receive_single",
         .payload = "second payload"}
    };
    test_range_eq(notifs2, expected2);
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
    auto [ec, notifs] = co_await conn.receive();
    if (!check_success(ec, {}))
        co_return;

    // Check
    const protocol::notification_response expected[] = {
        {.process_id = notifier.state().backend_process_id,
         .channel_name = "test_empty_payload",
         .payload = ""}
    };
    test_range_eq(notifs, expected);
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
    auto [ec, notifs] = co_await conn.receive();
    if (!check_success(ec, {}))
        co_return;

    // Check
    const protocol::notification_response expected[] = {
        {.process_id = conn.state().backend_process_id,
         .channel_name = "test_during_exec",
         .payload = "during exec"}
    };
    test_range_eq(notifs, expected);
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
    auto [ec, notifs] = co_await conn.receive();
    if (!check_success(ec, {}))
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
    auto [ec, notifs] = co_await conn.receive();
    if (!check_success(ec, {}))
        co_return;

    // Check
    const auto notifier_pid = notifier.state().backend_process_id;
    const protocol::notification_response expected[] = {
        {.process_id = notifier_pid, .channel_name = "test_batch", .payload = "first" },
        {.process_id = notifier_pid, .channel_name = "test_batch", .payload = "second"},
    };
    test_range_eq(notifs, expected);
}

//
// Synchronization between exec() and receive()
//

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
            auto [ec, notifs] = co_await conn.receive();
            if (check_success(ec))
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
            check_success(co_await select_finished.wait());
            co_await checked_exec(
                notifier,
                request().add_query("NOTIFY test_receive_during_exec, 'my payload'")
            );

            co_return {};
        }()
    ));
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
            auto [ec, notifs] = co_await conn.receive();
            if (check_success(ec))
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
}

// // Same, with a second exec queued behind the one holding the reader. The receive()
// // must not be starved by it, and the queued exec must still get its own response
// capy::task<> test_receive_starts_during_exec_with_queued()
// {
//     // Setup
//     auto conn = co_await establish_connection(), notifier = co_await establish_connection();
//     auto conn_lock = co_await acquire_advisory_lock(11);

//     // Listen
//     if (!co_await checked_exec(conn, request().add_query("LISTEN \"test_receive_queued\"")))
//         co_return;

//     // Blocks on the lock, keeping the reader busy
//     request req_exec1;
//     req_exec1.add_query("SELECT $1 AS value", 42);
//     req_exec1.add_query("SELECT pg_advisory_lock($1)", 11);

//     // Queued behind req_exec1
//     request req_exec2;
//     req_exec2.add_query("SELECT $1 AS value", 50);

//     capy::async_event select_finished, notify_sent;

//     // Recall that when_all launches things in order
//     static_cast<void>(co_await capy::when_all(
//         [&]() -> capy::io_task<> {
//             // Owns the reader until the lock is released
//             row_int row{};
//             auto cb = [&](row_int r) {
//                 row = r;
//                 select_finished.set();
//             };
//             auto [dummy, err] = co_await do_exec(
//                 conn,
//                 req_exec1,
//                 response{resultset_callback<row_int>(cb), check_execute()}
//             );
//             check_success(err);
//             BOOST_TEST_EQ(row, row_int{.value = 42});
//             co_return {};
//         }(),

//         [&]() -> capy::io_task<> {
//             // Starts with the exec above already holding the reader, so it has to wait
//             auto [ec, notifs] = co_await conn.receive();
//             if (check_success(ec, {}))
//             {
//                 const protocol::notification_response expected[] = {
//                     {.process_id = notifier.state().backend_process_id,
//                      .channel_name = "test_receive_queued",
//                      .payload = "with a queued exec"}
//                 };
//                 test_range_eq(notifs, expected);
//             }
//             co_return {};
//         }(),

//         [&]() -> capy::io_task<> {
//             // Enters the queue after the receive(), and should get its own response
//             std::vector<row_int> rows;
//             auto [dummy, err] = co_await do_exec(conn, req_exec2, into(rows));
//             check_success(err);
//             test_range_eq(rows, std::vector<row_int>{{.value = 50}});
//             co_return {};
//         }(),

//         [&]() -> capy::io_task<> {
//             // Once the first exec is blocked on the lock, raise the notification
//             auto [ec] = co_await select_finished.wait();
//             BOOST_TEST_EQ(ec, std::error_code());
//             static_cast<void>(co_await checked_exec(
//                 notifier,
//                 request().add_query("NOTIFY test_receive_queued, 'with a queued exec'")
//             ));
//             notify_sent.set();
//             co_return {};
//         }(),

//         [&]() -> capy::io_task<> {
//             // Release the lock so both execs can complete
//             auto [ec] = co_await notify_sent.wait();
//             BOOST_TEST_EQ(ec, std::error_code());
//             check_success(co_await conn_lock.shutdown(), {});
//             co_return {};
//         }()
//     ));
// }

// // An exec() started while a receive() owns the reader. The exec's response
// // messages reach the receiver, which has to hand the reader over rather than
// // consume them
// capy::task<> test_exec_starts_during_receive()
// {
//     // Setup
//     auto conn = co_await establish_connection(), notifier = co_await establish_connection();
//     auto conn_lock = co_await acquire_advisory_lock(12);

//     // Listen
//     if (!co_await checked_exec(conn, request().add_query("LISTEN \"test_exec_during_receive\"")))
//         co_return;

//     // Blocks on the lock, so the exec stays in flight while we set things up
//     request req_exec;
//     req_exec.add_query("SELECT $1 AS value", 42);
//     req_exec.add_query("SELECT pg_advisory_lock($1)", 12);

//     capy::async_event select_finished, notify_sent;

//     // when_all launches things in order, so the receive() runs first and takes
//     // the reader before the exec below registers itself
//     static_cast<void>(co_await capy::when_all(
//         [&]() -> capy::io_task<> {
//             // Owns the reader until the exec's first response message arrives
//             auto [ec, notifs] = co_await conn.receive();
//             if (check_success(ec, {}))
//             {
//                 const protocol::notification_response expected[] = {
//                     {.process_id = notifier.state().backend_process_id,
//                      .channel_name = "test_exec_during_receive",
//                      .payload = "exec started later"}
//                 };
//                 test_range_eq(notifs, expected);
//             }
//             co_return {};
//         }(),

//         [&]() -> capy::io_task<> {
//             // Its response messages are the ones that force the receiver to yield
//             row_int row{};
//             auto cb = [&](row_int r) {
//                 row = r;
//                 select_finished.set();
//             };
//             auto [dummy, err] = co_await do_exec(
//                 conn,
//                 req_exec,
//                 response{resultset_callback<row_int>(cb), check_execute()}
//             );
//             check_success(err);
//             BOOST_TEST_EQ(row, row_int{.value = 42});
//             co_return {};
//         }(),

//         [&]() -> capy::io_task<> {
//             // Once the exec is blocked on the lock, raise the notification
//             auto [ec] = co_await select_finished.wait();
//             BOOST_TEST_EQ(ec, std::error_code());
//             static_cast<void>(co_await checked_exec(
//                 notifier,
//                 request().add_query("NOTIFY test_exec_during_receive, 'exec started later'")
//             ));
//             notify_sent.set();
//             co_return {};
//         }(),

//         [&]() -> capy::io_task<> {
//             // Release the lock so the exec can complete
//             auto [ec] = co_await notify_sent.wait();
//             BOOST_TEST_EQ(ec, std::error_code());
//             check_success(co_await conn_lock.shutdown(), {});
//             co_return {};
//         }()
//     ));
// }

}  // namespace

int main()
{
    run_coroutine_test(test_single_notification());
    run_coroutine_test(test_empty_payload());
    run_coroutine_test(test_notification_during_exec());
    run_coroutine_test(test_exec_doesnt_invalidate_notifications());
    run_coroutine_test(test_batch_notifications());

    run_coroutine_test(test_receive_during_exec_handover());
    run_coroutine_test(test_receive_during_exec_gets_notifications());
    // run_coroutine_test(test_receive_starts_during_exec_with_queued());
    // run_coroutine_test(test_exec_starts_during_receive());

    return boost::report_errors();
}
