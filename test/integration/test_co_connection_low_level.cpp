//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/capy/cond.hpp>
#include <boost/capy/ex/async_event.hpp>
#include <boost/capy/ex/run.hpp>
#include <boost/capy/io_task.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/when_all.hpp>
#include <boost/core/lightweight_test.hpp>
#include <boost/describe/class.hpp>
#include <boost/describe/operators.hpp>

#include <cstddef>
#include <cstdint>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "nativepg/client_errc.hpp"
#include "nativepg/co_connection.hpp"
#include "nativepg/exec_state.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/notification_vector.hpp"
#include "nativepg/protocol/async.hpp"
#include "nativepg/request.hpp"
#include "nativepg/responses/any_request_message.hpp"
#include "nativepg/responses/check.hpp"
#include "nativepg/responses/into.hpp"
#include "nativepg/responses/response.hpp"
#include "nativepg/responses/response_handler.hpp"
#include "test_utils/co_connection_utils.hpp"
#include "test_utils/corosio_utils.hpp"
#include "test_utils/printing.hpp"
#include "test_utils/test_cond_eq.hpp"
#include "test_utils/test_range_eq.hpp"
#include "test_utils/yield.hpp"

namespace capy = boost::capy;
using namespace nativepg;
using namespace nativepg::test;
using namespace std::string_view_literals;

namespace {

struct row_int
{
    int value;
};
BOOST_DESCRIBE_STRUCT(row_int, (), (value))

struct row_string
{
    std::string value;
};
BOOST_DESCRIBE_STRUCT(row_string, (), (value))

using boost::describe::operators::operator==;
using boost::describe::operators::operator<<;

//
// Usual cases
//

// Sunny-day case. This executes write fully before reading, as opposed to exec()
capy::task<> test_success()
{
    // Setup
    auto conn = co_await establish_connection();
    request req;
    req.add_query("SELECT $1 + $2 AS value", 42, 10);
    req.add_query("SELECT 90 AS value");
    std::vector<row_int> rows1, rows2;
    response handler{into(rows1), into(rows2)};

    // A fresh state holds nothing
    exec_state st;
    BOOST_TEST(!st.is_prepared());
    BOOST_TEST(!st.write_done());
    BOOST_TEST(!st.read_done());

    // Prepare
    if (!BOOST_TEST_EQ(conn.prepare_request(st, req, &handler), std::error_code()))
        co_return;
    BOOST_TEST(st.is_prepared());
    BOOST_TEST(!st.write_done());
    BOOST_TEST(!st.read_done());

    // Write the request
    if (!check_success(co_await conn.write_request(st)))
        co_return;
    BOOST_TEST(st.is_prepared());
    BOOST_TEST(st.write_done());
    BOOST_TEST(!st.read_done());

    // Read the response. A single call only guarantees progress, not completion
    while (!st.read_done())
    {
        if (!check_success(co_await conn.read_some_response(st)))
            co_return;
    }

    // Check what the handler produced
    check_success(st.handler_error());
    test_range_eq(rows1, std::vector<row_int>{{.value = 52}});
    test_range_eq(rows2, std::vector<row_int>{{.value = 90}});

    // Having read the response in full, we left nothing behind
    co_await check_connection_usable(conn);
}

// An error reported by the handler while reading is surfaced by the operation
// and recorded in the exec_state
capy::task<> test_handler_error()
{
    // A response handler that reports a well-known error
    struct failing_handler
    {
        handler_setup_result setup(const request& req, std::size_t) { return req.messages().size(); }

        void on_message(const any_request_message&, std::size_t, extended_error& err)
        {
            err.code = std::make_error_code(std::errc::invalid_argument);
            err.diag = diagnostics("some_error");
        }
    };

    // Setup
    auto conn = co_await establish_connection();

    // The query itself is valid: the error comes from the handler
    request req;
    req.add_query("SELECT $1 AS value", 42);
    failing_handler handler;

    exec_state st;
    if (!BOOST_TEST_EQ(conn.prepare_request(st, req, &handler), std::error_code()))
        co_return;
    if (!check_success(co_await conn.write_request(st)))
        co_return;

    // In the low-level API, handler errors only appear in the exec_state
    while (!st.read_done())
    {
        if (!check_success(co_await conn.read_some_response(st)))
            co_return;
    }

    // The error is reported by the operation and recorded in the state
    BOOST_TEST(st.write_done());
    BOOST_TEST(st.read_done());
    const extended_error expected_err{
        std::make_error_code(std::errc::invalid_argument),
        diagnostics("some_error")
    };
    BOOST_TEST_EQ(st.handler_error(), expected_err);

    // A handler error is not a protocol error: we read the response in full
    co_await check_connection_usable(conn);
}

// prepare_request fails if the handler's setup fails, leaving the state unprepared
capy::task<> test_prepare_request_error()
{
    // Setup
    auto conn = co_await establish_connection();

    request req;
    req.add_query("SELECT $1 AS value", 42);
    req.add_query("SELECT $1 AS value", 50);
    check_execute handler;  // incompatible
    exec_state st;

    BOOST_TEST_EQ(
        conn.prepare_request(st, req, &handler),
        std::error_code(client_errc::incompatible_response_length)
    );
    BOOST_TEST_NOT(st.is_prepared());
    BOOST_TEST_NOT(st.read_done());
    BOOST_TEST_NOT(st.write_done());

    // Nothing was written, so the connection is untouched
    co_await check_connection_usable(conn);
}

//
// Partial reads, sequencing, retries...
//

// Instead of blocking, read_some_response returns when only some messages are read
// in one read_some
capy::task<> test_partial_read()
{
    // Setup
    auto conn = co_await establish_connection(), locker = co_await establish_connection();

    // Take the lock, so the second query in our request can't complete.
    // Note: different tests should use different IDs
    constexpr std::int64_t lock_id = 21;
    if (!co_await checked_exec(locker, request().add_query("SELECT pg_advisory_lock($1)", lock_id)))
        co_return;

    // The first query answers right away, the second one blocks on the lock
    request req;
    req.add_query("SELECT $1 AS value", 42);
    req.add_query("SELECT pg_advisory_lock($1)", lock_id);
    std::vector<row_int> rows;
    response handler{into(rows), check_execute()};

    // Prepare and write
    exec_state st;
    if (!BOOST_TEST_EQ(conn.prepare_request(st, req, &handler), std::error_code()))
        co_return;
    if (!check_success(co_await conn.write_request(st)))
        co_return;

    // Read until the first resultset arrives. The server still owes us the second
    // one at this point, so read_some_response had to return without completing
    while (rows.empty())
    {
        if (!check_success(co_await conn.read_some_response(st)))
            co_return;
    }
    BOOST_TEST(!st.read_done());
    test_range_eq(rows, std::vector<row_int>{{.value = 42}});

    // Unblock the rest of the response
    if (!check_success(co_await locker.shutdown()))
        co_return;

    // Read the remainder
    while (!st.read_done())
    {
        if (!check_success(co_await conn.read_some_response(st)))
            co_return;
    }

    // Check
    check_success(st.handler_error());

    co_await check_connection_usable(conn);
}

// Starting the reader first is OK
capy::task<> test_read_before_write()
{
    // Setup
    auto conn = co_await establish_connection();

    request req;
    req.add_query("SELECT $1 AS value", 42);
    std::vector<row_int> rows;
    auto handler = into(rows);

    exec_state st;
    if (!BOOST_TEST_EQ(conn.prepare_request(st, req, &handler), std::error_code()))
        co_return;

    static_cast<void>(co_await capy::when_all(
        [&]() -> capy::io_task<> {
            while (!st.read_done())
            {
                if (!check_success(co_await conn.read_some_response(st)))
                    co_return {};
            }
            co_return {};
        }(),

        [&]() -> capy::io_task<> {
            // Just in case
            co_await yield();

            // Nothing has been written yet, so the reader can't have made progress
            BOOST_TEST(!st.write_done());
            BOOST_TEST(!st.read_done());
            BOOST_TEST(rows.empty());

            // This is what unblocks the reader
            check_success(co_await conn.write_request(st));
            BOOST_TEST(st.write_done());
            co_return {};
        }()
    ));

    // Check
    BOOST_TEST(st.read_done());
    check_success(st.handler_error());
    test_range_eq(rows, std::vector<row_int>{{.value = 42}});

    co_await check_connection_usable(conn);
}

// A cancelled write isn't terminal: the state records what made it to the
// server, and the operation can be retried to send the rest
capy::task<> test_retry_write()
{
    // Setup
    auto conn = co_await establish_connection();

    request req;
    req.add_query("SELECT $1 AS value", 42);
    std::vector<row_int> rows;
    auto handler = into(rows);

    exec_state st;
    if (!BOOST_TEST_EQ(conn.prepare_request(st, req, &handler), std::error_code()))
        co_return;

    // Run the write under a token that is already stopped, so it reports
    // cancellation however much of the request it managed to send
    std::stop_source stop_src;
    stop_src.request_stop();
    auto [write_ec] = co_await capy::run(stop_src.get_token())(conn.write_request(st));
    test_cond_eq(write_ec, capy::cond::canceled);

    // We still hold the write side, so no other request can interleave with ours
    BOOST_TEST_NOT(st.write_done());

    // Retrying sends whatever didn't make it the first time
    if (!check_success(co_await conn.write_request(st)))
        co_return;
    BOOST_TEST(st.write_done());

    // The server received the request exactly once, so the response matches
    while (!st.read_done())
    {
        if (!check_success(co_await conn.read_some_response(st)))
            co_return;
    }
    check_success(st.handler_error());
    test_range_eq(rows, std::vector<row_int>{{.value = 42}});

    co_await check_connection_usable(conn);
}

// Same for the reader: a cancelled read keeps whatever progress it made and
// can be retried to finish the response
capy::task<> test_retry_read_after_cancel()
{
    // Setup
    auto conn = co_await establish_connection(), locker = co_await establish_connection();

    // Take the lock, so the second query can't answer until we say so.
    // Note: different tests should use different IDs
    constexpr std::int64_t lock_id = 25;
    if (!co_await checked_exec(locker, request().add_query("SELECT pg_advisory_lock($1)", lock_id)))
        co_return;

    request req;
    req.add_query("SELECT $1 AS value", 42);
    req.add_query("SELECT pg_advisory_lock($1)", lock_id);
    std::vector<row_int> rows;
    response handler{into(rows), check_execute()};

    exec_state st;
    if (!BOOST_TEST_EQ(conn.prepare_request(st, req, &handler), std::error_code()))
        co_return;
    if (!check_success(co_await conn.write_request(st)))
        co_return;

    // Read the first resultset. The second one is stuck on the lock
    while (rows.empty())
    {
        if (!check_success(co_await conn.read_some_response(st)))
            co_return;
    }
    test_range_eq(rows, std::vector<row_int>{{.value = 42}});
    BOOST_TEST_NOT(st.read_done());

    // Cancel a read that is waiting for the rest of the response
    static_cast<void>(co_await capy::when_all(
        [&]() -> capy::io_task<> {
            auto [ec] = co_await conn.read_some_response(st);
            test_cond_eq(ec, capy::cond::canceled);
            co_return {};
        }(),

        [&]() -> capy::io_task<> {
            co_await yield();  // let the reader start
            co_return {std::make_error_code(std::errc::io_error)};
        }()
    ));

    // The cancellation didn't release the read side
    BOOST_TEST_NOT(st.read_done());

    // Unblock the server and retry until completion
    if (!check_success(co_await locker.shutdown()))
        co_return;
    while (!st.read_done())
    {
        if (!check_success(co_await conn.read_some_response(st)))
            co_return;
    }
    check_success(st.handler_error());

    co_await check_connection_usable(conn);
}

//
// Interaction with exec() and receive()
//

// The low-level API waits if an exec() is in flight
capy::task<> test_exec_before()
{
    // Setup
    auto conn = co_await establish_connection(), locker = co_await establish_connection();

    // Take the lock, so the exec() below can't finish.
    // Note: different tests should use different IDs
    constexpr std::int64_t lock_id = 22;
    if (!co_await checked_exec(locker, request().add_query("SELECT pg_advisory_lock($1)", lock_id)))
        co_return;

    // Runs through exec(), and blocks on the lock after its first resultset
    request req_exec;
    req_exec.add_query("SELECT $1 AS value", 42);
    req_exec.add_query("SELECT pg_advisory_lock($1)", lock_id);
    std::vector<row_int> exec_rows;

    // Runs through the low-level API, and has to queue behind the exec().
    request req_ll;
    req_ll.add_query("SELECT $1 AS value", "abcd");
    std::vector<row_string> ll_rows;  // detect mis-routed responses (!= type)
    auto ll_handler = into(ll_rows);
    exec_state st;

    static_cast<void>(co_await capy::when_all(
        [&]() -> capy::io_task<> {
            // Execute the query. Will acquire ownership first and block on the lock
            response handler{into(exec_rows), check_execute()};
            check_success(co_await conn.exec(req_exec, &handler));
            co_return {};
        }(),

        [&]() -> capy::io_task<> {
            // Ensure exec() goes first
            co_await yield();

            // Setup
            if (!BOOST_TEST_EQ(conn.prepare_request(st, req_ll, &ll_handler), std::error_code()))
                co_return {};

            // We should be able to write even if exec() hasn't finished
            check_success(co_await conn.write_request(st));
            BOOST_TEST(st.write_done());
            BOOST_TEST_NOT(st.read_done());

            // Unblock exec()
            check_success(co_await locker.shutdown());

            // Now read
            while (!st.read_done())
            {
                if (!check_success(co_await conn.read_some_response(st)))
                    co_return {};
            }
            co_return {};
        }()
    ));

    // Each operation got its own response
    BOOST_TEST(st.read_done());
    check_success(st.handler_error());
    test_range_eq(exec_rows, std::vector<row_int>{{.value = 42}});
    test_range_eq(ll_rows, std::vector<row_string>{{.value = "abcd"}});

    co_await check_connection_usable(conn);
}

// The reverse: the low-level API waits if exec() is in-flight
capy::task<> test_exec_after()
{
    // Setup
    auto conn = co_await establish_connection(), locker = co_await establish_connection();

    // Note: different tests should use different IDs
    constexpr std::int64_t lock_id = 23;
    if (!co_await checked_exec(locker, request().add_query("SELECT pg_advisory_lock($1)", lock_id)))
        co_return;

    // Runs through the low-level API, and blocks on the lock after its first resultset
    request req_ll;
    req_ll.add_query("SELECT $1 AS value", 42);
    req_ll.add_query("SELECT pg_advisory_lock($1)", lock_id);
    std::vector<row_int> ll_rows;
    response ll_handler{into(ll_rows), check_execute()};
    exec_state st;

    // Queues behind it
    request req_exec;
    req_exec.add_query("SELECT $1 AS value", "abcd");
    std::vector<row_string> exec_rows;

    // Write the request
    if (!BOOST_TEST_EQ(conn.prepare_request(st, req_ll, &ll_handler), std::error_code()))
        co_return;
    if (!check_success(co_await conn.write_request(st)))
        co_return;

    // Read the 1st response (the 2nd one is waiting for the lock)
    while (!st.read_done() && !ll_rows.empty())
    {
        if (!check_success(co_await conn.read_some_response(st)))
            co_return;
    }
    BOOST_TEST(!st.read_done());

    // Start an exec() that will queue, and unlock the reader
    static_cast<void>(co_await capy::when_all(
        [&]() -> capy::io_task<> {
            check_success(co_await conn.exec(req_exec, into(exec_rows)));
            co_return {};
        }(),

        [&]() -> capy::io_task<> {
            co_await yield();  // Just in case

            // Unblock the reader
            check_success(co_await locker.shutdown());

            // Finish reading
            while (!st.read_done())
            {
                if (!check_success(co_await conn.read_some_response(st)))
                    co_return {};
            }
            BOOST_TEST(st.read_done());
            co_return {};
        }()
    ));

    // Each operation got its own response
    BOOST_TEST(st.read_done());
    check_success(st.handler_error());
    test_range_eq(ll_rows, std::vector<row_int>{{.value = 42}});
    test_range_eq(exec_rows, std::vector<row_string>{{.value = "abcd"}});

    co_await check_connection_usable(conn);
}

// A receive() is running when the low-level operation is started
capy::task<> test_receive_before()
{
    // Setup
    auto conn = co_await establish_connection(), notifier = co_await establish_connection();
    if (!co_await checked_exec(conn, request().add_query("LISTEN test_ll_receive_before")))
        co_return;

    // Runs through the low-level API, once the receive() is the current reader
    request req;
    req.add_query("SELECT $1 AS value", "abcd");
    std::vector<row_string> rows;
    auto ll_handler = into(rows);
    exec_state st;

    notification_vector notifs;

    static_cast<void>(co_await capy::when_all(
        [&]() -> capy::io_task<> {
            // Takes the read side, since nothing else is running
            check_success(co_await conn.receive(notifs));
            co_return {};
        }(),

        [&]() -> capy::io_task<> {
            // Ensure receive() goes first
            co_await yield();

            // Setup
            if (!BOOST_TEST_EQ(conn.prepare_request(st, req, &ll_handler), std::error_code()))
                co_return {};

            // The receive() hands the read side over as soon as it sees a message
            // that isn't a notification
            if (!check_success(co_await conn.write_request(st)))
                co_return {};
            while (!st.read_done())
            {
                if (!check_success(co_await conn.read_some_response(st)))
                    co_return {};
            }

            // The receive() is still waiting, so give it something to report
            co_await checked_exec(notifier, request().add_query("NOTIFY test_ll_receive_before, 'hello'"));
            co_return {};
        }()
    ));

    // Each operation got its own output
    check_success(st.handler_error());
    test_range_eq(rows, std::vector<row_string>{{.value = "abcd"}});
    const protocol::notification_response expected[] = {
        {.process_id = notifier.state().backend_process_id,
         .channel_name = "test_ll_receive_before",
         .payload = "hello"}
    };
    test_range_eq(notifs, expected);

    co_await check_connection_usable(conn);
}

// The reverse: a receive() started while a low-level operation owns the read
// side waits for it, and gets its notifications through it
capy::task<> test_receive_after()
{
    // Setup
    auto conn = co_await establish_connection(), locker = co_await establish_connection(),
         notifier = co_await establish_connection();
    if (!co_await checked_exec(conn, request().add_query("LISTEN test_ll_receive_after")))
        co_return;

    // Note: different tests should use different IDs
    constexpr std::int64_t lock_id = 24;
    if (!co_await checked_exec(locker, request().add_query("SELECT pg_advisory_lock($1)", lock_id)))
        co_return;

    // Blocks on the lock after its first resultset, so it keeps the read side
    request req_ll;
    req_ll.add_query("SELECT $1 AS value", 42);
    req_ll.add_query("SELECT pg_advisory_lock($1)", lock_id);
    std::vector<row_int> ll_rows;
    response ll_handler{into(ll_rows), check_execute()};
    exec_state st;

    if (!BOOST_TEST_EQ(conn.prepare_request(st, req_ll, &ll_handler), std::error_code()))
        co_return;
    if (!check_success(co_await conn.write_request(st)))
        co_return;
    while (ll_rows.empty())
    {
        if (!check_success(co_await conn.read_some_response(st)))
            co_return;
    }
    BOOST_TEST_NOT(st.read_done());

    // Start a receive() that has to wait for us
    notification_vector notifs;
    bool receive_finished = false;

    static_cast<void>(co_await capy::when_all(
        [&]() -> capy::io_task<> {
            check_success(co_await conn.receive(notifs));
            receive_finished = true;
            co_return {};
        }(),

        [&]() -> capy::io_task<> {
            co_await yield();  // let the receive() start

            // We still own the read side, so the receive() can't have read anything
            BOOST_TEST_NOT(receive_finished);
            BOOST_TEST(notifs.empty());

            // Raise a notification. Our reader is the one that takes it off the
            // wire and hands it over to the receive()
            co_await checked_exec(notifier, request().add_query("NOTIFY test_ll_receive_after, 'hello'"));

            // Unblock the rest of our own response
            check_success(co_await locker.shutdown());
            while (!st.read_done())
            {
                if (!check_success(co_await conn.read_some_response(st)))
                    co_return {};
            }
            co_return {};
        }()
    ));

    // Each operation got its own output
    BOOST_TEST(st.read_done());
    check_success(st.handler_error());
    test_range_eq(ll_rows, std::vector<row_int>{{.value = 42}});
    const protocol::notification_response expected[] = {
        {.process_id = notifier.state().backend_process_id,
         .channel_name = "test_ll_receive_after",
         .payload = "hello"}
    };
    test_range_eq(notifs, expected);

    co_await check_connection_usable(conn);
}

//
// Abandonment
//

// Resetting a state that was never set up does nothing
capy::task<> test_reset_not_prepared()
{
    // Setup
    auto conn = co_await establish_connection();

    exec_state st;
    BOOST_TEST_NOT(st.is_prepared());
    st.reset();
    BOOST_TEST_NOT(st.is_prepared());

    // The connection never knew about it
    co_await check_connection_usable(conn);
}

// Resetting a state that was prepared but never started does nothing
capy::task<> test_reset_prepared()
{
    // Setup
    auto conn = co_await establish_connection();

    // Setting a session-scoped variable leaves a trace we can look for
    request req;
    req.add_query("SET SESSION nativepg.query_run = 'yes'");
    check handler;

    // current_setting will return NULL if the variable is not there
    request req_check;
    req_check.add_query("SELECT coalesce(current_setting('nativepg.query_run', true), 'no') AS value");

    exec_state st;
    if (!BOOST_TEST_EQ(conn.prepare_request(st, req, &handler), std::error_code()))
        co_return;
    BOOST_TEST(st.is_prepared());

    st.reset();
    BOOST_TEST_NOT(st.is_prepared());
    BOOST_TEST_NOT(st.write_done());
    BOOST_TEST_NOT(st.read_done());

    // The statement never made it to the server, so the variable isn't there
    std::vector<row_string> rows;
    if (!co_await checked_exec(conn, req_check, into(rows)))
        co_return;
    test_range_eq(rows, std::vector<row_string>{{.value = "no"}});
}

// Resetting after writing, without reading anything, leaves a response that
// nobody wants. The next request has to discard it before reading its own
capy::task<> test_reset_after_write()
{
    // Setup
    auto conn = co_await establish_connection();

    request req;
    req.add_query("SELECT $1 AS value", "abcd");
    std::vector<row_int> rows;
    auto handler = into(rows);

    exec_state st;
    if (!BOOST_TEST_EQ(conn.prepare_request(st, req, &handler), std::error_code()))
        co_return;
    if (!check_success(co_await conn.write_request(st)))
        co_return;
    BOOST_TEST(st.write_done());
    BOOST_TEST_NOT(st.read_done());

    // Abandon it. The server still owes us the whole response
    st.reset();
    BOOST_TEST_NOT(st.is_prepared());
    BOOST_TEST(rows.empty());

    co_await check_connection_usable(conn);
}

// Same, but abandoning with part of the response already read
capy::task<> test_reset_after_partial_read()
{
    // Setup
    auto conn = co_await establish_connection(), locker = co_await establish_connection();

    // Take the lock, so the second query can't answer while we hold it.
    // Note: different tests should use different IDs
    constexpr std::int64_t lock_id = 26;
    if (!co_await checked_exec(locker, request().add_query("SELECT pg_advisory_lock($1)", lock_id)))
        co_return;

    request req;
    req.add_query("SELECT $1 AS value", "abcd");
    req.add_query("SELECT pg_advisory_lock($1)", lock_id);
    std::vector<row_string> rows;
    response handler{into(rows), check_execute()};

    exec_state st;
    if (!BOOST_TEST_EQ(conn.prepare_request(st, req, &handler), std::error_code()))
        co_return;
    if (!check_success(co_await conn.write_request(st)))
        co_return;

    // Read the first resultset
    while (rows.empty())
    {
        if (!check_success(co_await conn.read_some_response(st)))
            co_return;
    }
    BOOST_TEST_NOT(st.read_done());
    test_range_eq(rows, std::vector<row_string>{{.value = "abcd"}});

    // Abandon with the second resultset outstanding
    st.reset();
    BOOST_TEST_NOT(st.is_prepared());

    // Let the server produce what we abandoned
    if (!check_success(co_await locker.shutdown()))
        co_return;

    co_await check_connection_usable(conn);
}

// exec_state destructor counts as abandonment
capy::task<> test_destructor_abandons()
{
    // Setup
    auto conn = co_await establish_connection();

    request req;
    req.add_query("SELECT $1 AS value", "abcd");
    std::vector<row_string> rows;
    auto handler = into(rows);

    {
        // Prepare and write, but don't read
        exec_state st;
        if (!BOOST_TEST_EQ(conn.prepare_request(st, req, &handler), std::error_code()))
            co_return;
        if (!check_success(co_await conn.write_request(st)))
            co_return;
        BOOST_TEST(st.write_done());
        BOOST_TEST_NOT(st.read_done());
    }

    // The request has been abandoned, but with enough info to keep the connection healthy
    co_await check_connection_usable(conn);
}

// // A state that has been driven to completion can be reused for another request
// capy::task<> test_reuse_state()
// {
//     // Setup
//     auto conn = co_await establish_connection();

//     exec_state st;

//     for (int expected : {1, 2})
//     {
//         request req;
//         req.add_query("SELECT $1 AS value", expected);
//         std::vector<row_int> rows;
//         auto handler = into(rows);

//         // prepare_request cleans up whatever the previous round left
//         if (!BOOST_TEST_EQ(conn.prepare_request(st, req, &handler), std::error_code()))
//             co_return;
//         if (!check_success(co_await conn.write_request(st)))
//             co_return;
//         while (!st.read_done())
//         {
//             if (!check_success(co_await conn.read_some_response(st)))
//                 co_return;
//         }

//         check_success(st.handler_error());
//         test_range_eq(rows, std::vector<row_int>{{.value = expected}});

//         // The request and the handler die at the end of this iteration, so the
//         // state must not outlive them holding references
//         st.reset();
//     }

//     co_await check_connection_usable(conn);
// }

}  // namespace

int main()
{
    run_coroutine_test(test_success());
    run_coroutine_test(test_handler_error());
    run_coroutine_test(test_prepare_request_error());

    run_coroutine_test(test_partial_read());
    run_coroutine_test(test_read_before_write());
    run_coroutine_test(test_retry_write());
    run_coroutine_test(test_retry_read_after_cancel());

    run_coroutine_test(test_reset_not_prepared());
    run_coroutine_test(test_reset_prepared());
    run_coroutine_test(test_reset_after_write());
    run_coroutine_test(test_reset_after_partial_read());
    run_coroutine_test(test_destructor_abandons());

    run_coroutine_test(test_exec_before());
    run_coroutine_test(test_exec_after());
    run_coroutine_test(test_receive_before());
    run_coroutine_test(test_receive_after());

    return boost::report_errors();
}
