//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/capy/ex/this_coro.hpp>
#include <boost/capy/results.hpp>
#include <boost/capy/task.hpp>
#include <boost/core/lightweight_test.hpp>
#include <boost/describe/class.hpp>

#include <system_error>

#include "nativepg/co_connection.hpp"
#include "nativepg/exec_state.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/notification_vector.hpp"
#include "nativepg/protocol/async.hpp"
#include "nativepg/protocol/connection_state.hpp"
#include "nativepg/protocol/ready_for_query.hpp"
#include "nativepg/request.hpp"
#include "nativepg/responses/check.hpp"
#include "nativepg/sqlstate_cond.hpp"
#include "test_utils/ci_server.hpp"
#include "test_utils/co_connection_utils.hpp"
#include "test_utils/corosio_utils.hpp"
#include "test_utils/printing.hpp"
#include "test_utils/test_cond_eq.hpp"
#include "test_utils/test_range_eq.hpp"

namespace capy = boost::capy;
using namespace nativepg;
using namespace nativepg::test;

namespace {

using protocol::transaction_status;

struct row_int
{
    int value;
};
BOOST_DESCRIBE_STRUCT(row_int, (), (value))

//
// Connection establishment
//

// A connection that was never connected reports idle
capy::task<> test_before_connect()
{
    co_connection conn{co_await capy::this_coro::executor};
    BOOST_TEST_NOT(conn.is_connected());
    BOOST_TEST_EQ(conn.txn_status(), transaction_status::idle);
}

// A connect() that fails leaves the status at idle
capy::task<> test_connect_error()
{
    // Setup: credentials that the server won't accept
    auto params = default_connect_params();
    params.password = "bad_password";

    co_connection conn{co_await capy::this_coro::executor};

    auto [ec] = co_await conn.connect(params);
    test_cond_eq(ec, sqlstate_cond::invalid_password);
    BOOST_TEST_NOT(conn.is_connected());
    BOOST_TEST_EQ(conn.txn_status(), transaction_status::idle);
}

// This is the case even after connect tears down a session with another TXN state
capy::task<> test_connect_error_after_connected()
{
    // Setup
    auto conn = co_await establish_connection();
    auto params = default_connect_params();
    params.password = "bad_password";

    // Change txn status
    if (!co_await checked_exec(conn, "BEGIN"))
        co_return;
    BOOST_TEST_EQ(conn.txn_status(), transaction_status::in_transaction);

    // Failed connect
    auto [ec] = co_await conn.connect(params);
    test_cond_eq(ec, sqlstate_cond::invalid_password);
    BOOST_TEST_NOT(conn.is_connected());
    BOOST_TEST_EQ(conn.txn_status(), transaction_status::idle);
}

// connect() sets the status to idle
capy::task<> test_connect_success()
{
    // Setup
    co_connection conn{co_await capy::this_coro::executor};
    diagnostics diag;

    // Connect
    if (!check_success(co_await conn.connect(default_connect_params(), &diag), diag))
        co_return;
    BOOST_TEST_EQ(conn.txn_status(), transaction_status::idle);
}

// This is the case even after connect tears down a session with another TXN state
capy::task<> test_connect_success_after_connected()
{
    // Setup
    auto conn = co_await establish_connection();
    diagnostics diag;

    // Change txn status
    if (!co_await checked_exec(conn, "BEGIN"))
        co_return;
    BOOST_TEST_EQ(conn.txn_status(), transaction_status::in_transaction);

    // Connect again
    if (!check_success(co_await conn.connect(default_connect_params(), &diag), diag))
        co_return;
    BOOST_TEST_EQ(conn.txn_status(), transaction_status::idle);

    co_await check_connection_usable(conn);
}

//
// Tracking during a session
//

// We support all the status values reported by the server
capy::task<> test_all_status()
{
    // Setup
    auto conn = co_await establish_connection();
    BOOST_TEST_EQ(conn.txn_status(), transaction_status::idle);

    // Opening a transaction block
    if (!co_await checked_exec(conn, "BEGIN"))
        co_return;
    BOOST_TEST_EQ(conn.txn_status(), transaction_status::in_transaction);

    // A statement that the server rejects poisons the block
    request req_bad;
    req_bad.add_query("SELECT * FROM nativepg_nonexistent_table");
    auto [ec] = co_await conn.exec(req_bad, check());
    test_cond_eq(ec, sqlstate_cond::undefined_table);
    BOOST_TEST_EQ(conn.txn_status(), transaction_status::failed);

    // Statements are rejected until the block is closed, and the status
    // stays as it was
    request req_select;
    req_select.add_query("SELECT 42");
    auto [ec2] = co_await conn.exec(req_select, check());
    test_cond_eq(ec2, sqlstate_cond::in_failed_sql_transaction);
    BOOST_TEST_EQ(conn.txn_status(), transaction_status::failed);

    // Rolling back brings us back to idle
    if (!co_await checked_exec(conn, "ROLLBACK"))
        co_return;
    BOOST_TEST_EQ(conn.txn_status(), transaction_status::idle);

    co_await check_connection_usable(conn);
}

// Leftovers read by exec() correctly account for txn status changes
capy::task<> test_exec_reads_leftovers()
{
    // Setup
    auto conn = co_await establish_connection();

    // Write a request that opens a transaction block, and abandon it
    // without reading any of its response
    request req_begin;
    req_begin.add_query("BEGIN");
    check begin_handler;
    exec_state st;
    if (!BOOST_TEST_EQ(conn.prepare_request(st, req_begin, &begin_handler), std::error_code()))
        co_return;
    if (!check_success(co_await conn.write_request(st)))
        co_return;
    st.reset();

    // Sanity check: we haven't read anything yet, so the BEGIN is invisible to us
    BOOST_TEST_EQ(conn.txn_status(), transaction_status::idle);

    // Executing another request needs to skip previous responses.
    // The txn status update is correctly picked.
    if (!co_await checked_exec(conn, "SELECT 42"))
        co_return;
    BOOST_TEST_EQ(conn.txn_status(), transaction_status::in_transaction);

    co_await check_connection_usable(conn);
}

// Same for receive()
capy::task<> test_receive_reads_leftovers()
{
    // Setup
    auto conn = co_await establish_connection();

    // Listen and open a txn block. Needs to be outside any transaction block
    request req1;
    req1.add_query("LISTEN test_txn_status_leftovers");
    req1.add_query("BEGIN");
    if (!co_await checked_exec(conn, req1))
        co_return;
    BOOST_TEST_EQ(conn.txn_status(), transaction_status::in_transaction);

    // Write a request that closes the block and raises a notification,
    // and abandon it without reading any of its response
    request req2;
    req2.add_query("ROLLBACK");
    req2.add_query("NOTIFY test_txn_status_leftovers, 'after rollback'");
    check handler;
    exec_state st;
    if (!BOOST_TEST_EQ(conn.prepare_request(st, req2, &handler), std::error_code()))
        co_return;
    if (!check_success(co_await conn.write_request(st)))
        co_return;
    st.reset();

    // Read the leftovers and the notification
    notification_vector notifs;
    if (!check_success(co_await conn.receive(notifs)))
        co_return;

    const protocol::notification_response expected[] = {
        {.process_id = conn.state().backend_process_id,
         .channel_name = "test_txn_status_leftovers",
         .payload = "after rollback"}
    };
    test_range_eq(notifs, expected);

    // The ReadyForQuery found among the leftovers was applied
    BOOST_TEST_EQ(conn.txn_status(), transaction_status::idle);

    co_await check_connection_usable(conn);
}

//
// Session teardown
//

// A shutdown() discards the status, together with the rest of the session state
capy::task<> test_shutdown()
{
    // Setup
    auto conn = co_await establish_connection();

    // Open a txn block
    if (!co_await checked_exec(conn, "BEGIN"))
        co_return;
    BOOST_TEST_EQ(conn.txn_status(), transaction_status::in_transaction);

    // Shutdown should clear the status
    if (!check_success(co_await conn.shutdown()))
        co_return;
    BOOST_TEST_NOT(conn.is_connected());
    BOOST_TEST_EQ(conn.txn_status(), transaction_status::idle);
}

// A fatal error discards the status
capy::task<> test_fatal_error()
{
    // Setup
    auto conn = co_await establish_connection();

    // Get into a transaction block
    if (!co_await checked_exec(conn, "BEGIN"))
        co_return;
    BOOST_TEST_EQ(conn.txn_status(), transaction_status::in_transaction);

    // Ask the server to kill the backend serving us. It answers with a FATAL
    // error and closes the connection, so our reader never sees a ReadyForQuery
    request req;
    req.add_query("SELECT pg_terminate_backend(pg_backend_pid())");
    auto [ec] = co_await conn.exec(req, check());
    BOOST_TEST_NE(ec, std::error_code());  // check unreliable

    // The connection is dead, and the status it had no longer applies
    BOOST_TEST_NOT(conn.is_connected());
    BOOST_TEST_EQ(conn.txn_status(), transaction_status::idle);
}

}  // namespace

int main()
{
    run_coroutine_test(test_before_connect());
    run_coroutine_test(test_connect_error());
    run_coroutine_test(test_connect_error_after_connected());
    run_coroutine_test(test_connect_success());
    run_coroutine_test(test_connect_success_after_connected());

    run_coroutine_test(test_all_status());
    run_coroutine_test(test_exec_reads_leftovers());
    run_coroutine_test(test_receive_reads_leftovers());

    run_coroutine_test(test_shutdown());
    run_coroutine_test(test_fatal_error());

    return boost::report_errors();
}
