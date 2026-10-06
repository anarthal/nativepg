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
#include "nativepg/encoding.hpp"
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
#include "nativepg/write_status.hpp"
#include "test_utils/co_connection_utils.hpp"
#include "test_utils/corosio_utils.hpp"
#include "test_utils/exec_state_utils.hpp"
#include "test_utils/printing.hpp"
#include "test_utils/test_cond_eq.hpp"
#include "test_utils/test_opt_eq.hpp"
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
    check_status(st, {.is_prepared = false, .write_phase = write_status::request, .reader_done = false});

    // Prepare
    if (!BOOST_TEST_EQ(conn.prepare_request(st, req, &handler), std::error_code()))
        co_return;
    check_status(st, {.is_prepared = true, .write_phase = write_status::request, .reader_done = false});

    // Write the request
    if (!check_success(co_await conn.write_request(st)))
        co_return;
    check_status(st, {.is_prepared = true, .write_phase = write_status::done, .reader_done = false});

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
    check_status(st, {.is_prepared = true, .write_phase = write_status::done, .reader_done = true});
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
    check_status(st, {.is_prepared = false, .write_phase = write_status::request, .reader_done = false});

    // Nothing was written, so the connection is untouched
    co_await check_connection_usable(conn);
}

// Parameters reported by the server while reading a response are applied
capy::task<> test_gucs()
{
    // Setup
    auto conn = co_await establish_connection();

    // Sanity check: the test would pass vacuously if this were already the case
    BOOST_TEST(conn.client_encoding() != encoding::latin1);

    // Changing the parameter makes the server report it back as part of the response
    request req;
    req.add_query("SET SESSION client_encoding TO 'LATIN1'");
    check handler;

    exec_state st;
    if (!BOOST_TEST_EQ(conn.prepare_request(st, req, &handler), std::error_code()))
        co_return;
    if (!check_success(co_await conn.write_request(st)))
        co_return;
    while (!st.read_done())
    {
        if (!check_success(co_await conn.read_some_response(st)))
            co_return;
    }
    check_success(st.handler_error());

    // The reader picked the ParameterStatus up on its way through the response
    test_opt_eq(conn.client_encoding(), encoding::latin1);
}

// Parameters reported among the leftovers of an abandoned operation are applied
// by whoever ends up discarding them
capy::task<> test_gucs_leftovers()
{
    // Setup
    auto conn = co_await establish_connection();
    BOOST_TEST(conn.client_encoding() != encoding::latin1);

    // Write a request that changes the parameter, and abandon it without reading.
    // The ParameterStatus it produces is now owed to whoever reads next
    {
        request req;
        req.add_query("SET SESSION client_encoding TO 'LATIN1'");
        check handler;

        exec_state st;
        if (!BOOST_TEST_EQ(conn.prepare_request(st, req, &handler), std::error_code()))
            co_return;
        if (!check_success(co_await conn.write_request(st)))
            co_return;
    }

    // This one has to skip the abandoned response before reading its own,
    // and applies the parameter it finds on the way
    request req2;
    req2.add_query("SELECT $1 AS value", "abcd");
    std::vector<row_string> rows;
    auto handler2 = into(rows);

    exec_state st2;
    if (!BOOST_TEST_EQ(conn.prepare_request(st2, req2, &handler2), std::error_code()))
        co_return;
    if (!check_success(co_await conn.write_request(st2)))
        co_return;
    while (!st2.read_done())
    {
        if (!check_success(co_await conn.read_some_response(st2)))
            co_return;
    }

    // It read its own response, and tracked the parameter from the abandoned one
    check_success(st2.handler_error());
    test_range_eq(rows, std::vector<row_string>{{.value = "abcd"}});
    test_opt_eq(conn.client_encoding(), encoding::latin1);

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
    check_status(st, {.is_prepared = true, .write_phase = write_status::done, .reader_done = false});
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
            check_status(
                st,
                {.is_prepared = true, .write_phase = write_status::request, .reader_done = false}
            );
            BOOST_TEST(rows.empty());

            // This is what unblocks the reader
            check_success(co_await conn.write_request(st));
            check_status(st, {.is_prepared = true, .write_phase = write_status::done, .reader_done = false});
            co_return {};
        }()
    ));

    // Check
    check_status(st, {.is_prepared = true, .write_phase = write_status::done, .reader_done = true});
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
    check_status(st, {.is_prepared = true, .write_phase = write_status::request, .reader_done = false});

    // Retrying sends whatever didn't make it the first time
    if (!check_success(co_await conn.write_request(st)))
        co_return;
    check_status(st, {.is_prepared = true, .write_phase = write_status::done, .reader_done = false});

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
    check_status(st, {.is_prepared = true, .write_phase = write_status::done, .reader_done = false});

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
    check_status(st, {.is_prepared = true, .write_phase = write_status::done, .reader_done = false});

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
            check_status(st, {.is_prepared = true, .write_phase = write_status::done, .reader_done = false});

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
    check_status(st, {.is_prepared = true, .write_phase = write_status::done, .reader_done = true});
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
    check_status(st, {.is_prepared = true, .write_phase = write_status::done, .reader_done = false});

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
            check_status(st, {.is_prepared = true, .write_phase = write_status::done, .reader_done = true});
            co_return {};
        }()
    ));

    // Each operation got its own response
    check_status(st, {.is_prepared = true, .write_phase = write_status::done, .reader_done = true});
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
    check_status(st, {.is_prepared = true, .write_phase = write_status::done, .reader_done = false});

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
    check_status(st, {.is_prepared = true, .write_phase = write_status::done, .reader_done = true});
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
    check_status(st, {.is_prepared = false, .write_phase = write_status::request, .reader_done = false});
    st.reset();
    check_status(st, {.is_prepared = false, .write_phase = write_status::request, .reader_done = false});

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
    check_status(st, {.is_prepared = true, .write_phase = write_status::request, .reader_done = false});

    st.reset();
    check_status(st, {.is_prepared = false, .write_phase = write_status::request, .reader_done = false});

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
    check_status(st, {.is_prepared = true, .write_phase = write_status::done, .reader_done = false});

    // Abandon it. The server still owes us the whole response
    st.reset();
    check_status(st, {.is_prepared = false, .write_phase = write_status::request, .reader_done = false});
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
    check_status(st, {.is_prepared = true, .write_phase = write_status::done, .reader_done = false});
    test_range_eq(rows, std::vector<row_string>{{.value = "abcd"}});

    // Abandon with the second resultset outstanding
    st.reset();
    check_status(st, {.is_prepared = false, .write_phase = write_status::request, .reader_done = false});

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
        check_status(st, {.is_prepared = true, .write_phase = write_status::done, .reader_done = false});
    }

    // The request has been abandoned, but with enough info to keep the connection healthy
    co_await check_connection_usable(conn);
}

// A state that was left mid-operation can be reused: prepare_request releases
// whatever the previous round still held
capy::task<> test_reuse_state()
{
    // Setup
    auto conn = co_await establish_connection();

    exec_state st;

    // First round: write a request and abandon it without reading
    request req1;
    req1.add_query("SELECT $1 AS value", 42);
    check handler1;
    if (!BOOST_TEST_EQ(conn.prepare_request(st, req1, &handler1), std::error_code()))
        co_return;
    if (!check_success(co_await conn.write_request(st)))
        co_return;
    check_status(st, {.is_prepared = true, .write_phase = write_status::done, .reader_done = false});

    // Second round on the same state. A distinct row type makes it obvious
    // if we end up reading the response the first round left behind
    request req2;
    req2.add_query("SELECT $1 AS value", "abcd");
    std::vector<row_string> rows;
    auto handler2 = into(rows);
    if (!BOOST_TEST_EQ(conn.prepare_request(st, req2, &handler2), std::error_code()))
        co_return;
    check_status(st, {.is_prepared = true, .write_phase = write_status::request, .reader_done = false});

    if (!check_success(co_await conn.write_request(st)))
        co_return;
    while (!st.read_done())
    {
        if (!check_success(co_await conn.read_some_response(st)))
            co_return;
    }

    // We got our own response, with the abandoned one discarded along the way
    check_success(st.handler_error());
    test_range_eq(rows, std::vector<row_string>{{.value = "abcd"}});

    co_await check_connection_usable(conn);
}

//
// State checks
//

// write_request requires a state that has been prepared
capy::task<> test_write_not_prepared()
{
    // Setup
    auto conn = co_await establish_connection();

    exec_state st;
    auto [ec] = co_await conn.write_request(st);
    BOOST_TEST_EQ(ec, make_error_code(client_errc::invalid_state));

    co_await check_connection_usable(conn);
}

// Only one write_request per state may be in flight
capy::task<> test_write_already_running()
{
    // Setup
    auto conn = co_await establish_connection();

    request req;
    req.add_query("SELECT $1 AS value", 42);
    check handler;
    exec_state st;
    if (!BOOST_TEST_EQ(conn.prepare_request(st, req, &handler), std::error_code()))
        co_return;

    static_cast<void>(co_await capy::when_all(
        [&]() -> capy::io_task<> {
            // Writes successfully
            check_success(co_await conn.write_request(st));
            co_return {};
        }(),

        [&]() -> capy::io_task<> {
            // Launched after the first write
            auto [ec] = co_await conn.write_request(st);
            BOOST_TEST_EQ(ec, make_error_code(client_errc::already_running));
            co_return {};
        }()
    ));

    check_status(st, {.is_prepared = true, .write_phase = write_status::done, .reader_done = false});

    // After abandonment, the connection is usable
    st.reset();
    co_await check_connection_usable(conn);
}

// write_request requires a state that hasn't been fully written
capy::task<> test_write_already_done()
{
    // Setup
    auto conn = co_await establish_connection();

    request req;
    req.add_query("SELECT $1 AS value", 42);
    check handler;

    exec_state st;
    if (!BOOST_TEST_EQ(conn.prepare_request(st, req, &handler), std::error_code()))
        co_return;
    if (!check_success(co_await conn.write_request(st)))
        co_return;
    check_status(st, {.is_prepared = true, .write_phase = write_status::done, .reader_done = false});

    // Writing again would send the request twice
    auto [ec] = co_await conn.write_request(st);
    BOOST_TEST_EQ(ec, make_error_code(client_errc::invalid_state));

    st.reset();
    co_await check_connection_usable(conn);
}

// read_some_response requires a state that has been prepared
capy::task<> test_read_not_prepared()
{
    // Setup
    auto conn = co_await establish_connection();

    exec_state st;
    auto [ec] = co_await conn.read_some_response(st);
    BOOST_TEST_EQ(ec, make_error_code(client_errc::invalid_state));

    co_await check_connection_usable(conn);
}

// Only one read_some_response per state may be in flight
capy::task<> test_read_already_running()
{
    // Setup
    auto conn = co_await establish_connection(), locker = co_await establish_connection();

    // Take the lock, so our reader parks after the first resultset.
    // Note: different tests should use different IDs
    constexpr std::int64_t lock_id = 27;
    if (!co_await checked_exec(locker, request().add_query("SELECT pg_advisory_lock($1)", lock_id)))
        co_return;

    request req;
    req.add_query("SELECT $1 AS value", 42);
    req.add_query("SELECT pg_advisory_lock($1)", lock_id);

    std::vector<row_int> rows;
    response handler{into(rows), check_execute()};

    // Setup and write
    exec_state st;
    if (!BOOST_TEST_EQ(conn.prepare_request(st, req, &handler), std::error_code()))
        co_return;
    if (!check_success(co_await conn.write_request(st)))
        co_return;

    // Read the 1st resultset
    while (rows.empty())
    {
        if (!check_success(co_await conn.read_some_response(st)))
            co_return;
    }
    test_range_eq(rows, std::vector<row_int>{{.value = 42}});

    // Reading further will block
    static_cast<void>(co_await capy::when_all(
        [&]() -> capy::io_task<> {
            // This one will block and succeed
            while (!st.read_done())
            {
                if (!check_success(co_await conn.read_some_response(st)))
                    co_return {};
            }
            co_return {};
        }(),

        [&]() -> capy::io_task<> {
            co_await yield();  // let the read start

            // A second read on the same state is rejected
            auto [ec] = co_await conn.read_some_response(st);
            BOOST_TEST_EQ(ec, make_error_code(client_errc::already_running));

            // Let the one that is running finish
            check_success(co_await locker.shutdown());
            co_return {};
        }()
    ));

    // The rejected call didn't disturb the running one
    check_status(st, {.is_prepared = true, .write_phase = write_status::done, .reader_done = true});
    check_success(st.handler_error());

    co_await check_connection_usable(conn);
}

// read_some_response requires a state whose response hasn't been fully read
capy::task<> test_read_already_done()
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
    if (!check_success(co_await conn.write_request(st)))
        co_return;
    while (!st.read_done())
    {
        if (!check_success(co_await conn.read_some_response(st)))
            co_return;
    }

    // Reading again would consume somebody else's response
    auto [ec] = co_await conn.read_some_response(st);
    BOOST_TEST_EQ(ec, make_error_code(client_errc::invalid_state));

    co_await check_connection_usable(conn);
}

}  // namespace

int main()
{
    run_coroutine_test(test_success());
    run_coroutine_test(test_handler_error());
    run_coroutine_test(test_prepare_request_error());
    run_coroutine_test(test_gucs());
    run_coroutine_test(test_gucs_leftovers());

    run_coroutine_test(test_partial_read());
    run_coroutine_test(test_read_before_write());
    run_coroutine_test(test_retry_write());
    run_coroutine_test(test_retry_read_after_cancel());

    run_coroutine_test(test_exec_before());
    run_coroutine_test(test_exec_after());
    run_coroutine_test(test_receive_before());
    run_coroutine_test(test_receive_after());

    run_coroutine_test(test_reset_not_prepared());
    run_coroutine_test(test_reset_prepared());
    run_coroutine_test(test_reset_after_write());
    run_coroutine_test(test_reset_after_partial_read());
    run_coroutine_test(test_destructor_abandons());
    run_coroutine_test(test_reuse_state());

    run_coroutine_test(test_write_not_prepared());
    run_coroutine_test(test_write_already_running());
    run_coroutine_test(test_write_already_done());
    run_coroutine_test(test_read_not_prepared());
    run_coroutine_test(test_read_already_running());
    run_coroutine_test(test_read_already_done());

    return boost::report_errors();
}
