//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/assert/source_location.hpp>
#include <boost/capy/buffers/make_buffer.hpp>
#include <boost/capy/ex/this_coro.hpp>
#include <boost/capy/results.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/when_all.hpp>
#include <boost/core/lightweight_test.hpp>
#include <boost/describe/class.hpp>

#include <iostream>
#include <string_view>
#include <system_error>

#include "nativepg/capy-stub/test/buffer_source.hpp"
#include "nativepg/capy-stub/test/write_sink.hpp"
#include "nativepg/co_connection.hpp"
#include "nativepg/exclusivity.hpp"
#include "nativepg/exec_state.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/notification_vector.hpp"
#include "nativepg/request.hpp"
#include "nativepg/responses/check.hpp"
#include "nativepg/responses/response_handler.hpp"
#include "nativepg/responses/resultset_callback.hpp"
#include "nativepg/write_status.hpp"
#include "test_utils/ci_server.hpp"
#include "test_utils/co_connection_utils.hpp"
#include "test_utils/corosio_utils.hpp"
#include "test_utils/printing.hpp"

// Covers co_connection::has_ops_in_flight

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

// Drives a copy-in until the server asks us for data, so the tests covering
// the copy writer functions can start from there.
// req and handler must be kept alive until st is reset
capy::task<capy::results<>> enter_copy_data(
    co_connection& conn,
    exec_state& st,
    const request& req,
    response_handler_ref handler
)
{
    // A request that may enter copy-in mode requires exclusive access
    if (auto ec = conn.prepare_request(st, req, handler, exclusivity::exclusive))
        co_return {ec};
    if (auto [ec] = co_await conn.write_request(st); ec)
        co_return {ec};

    // Only the reader can tell us that the server accepted the COPY
    while (st.write_phase() == write_status::waiting_for_reader)
    {
        if (auto [ec] = co_await conn.read_some_response(st); ec)
            co_return {ec};
    }

    co_return {};
}

// Reads whatever is left of a response, so the connection is left usable
capy::task<capy::results<>> read_until_done(co_connection& conn, exec_state& st)
{
    while (!st.read_done())
    {
        if (auto [ec] = co_await conn.read_some_response(st); ec)
            co_return {ec};
    }
    co_return {};
}

//
// Connections with nothing running
//

// A connection that was never connected has nothing in flight
capy::task<> test_never_connected()
{
    co_connection conn{co_await capy::this_coro::executor};
    BOOST_TEST_NOT(conn.has_ops_in_flight());
}

// Neither does an established connection that is sitting idle
capy::task<> test_idle_connection()
{
    auto conn = co_await establish_connection();
    BOOST_TEST_NOT(conn.has_ops_in_flight());

    // A completed operation leaves nothing behind
    if (!co_await checked_exec(conn, "SELECT 42"))
        co_return;
    BOOST_TEST_NOT(conn.has_ops_in_flight());
}

// Nor one that has been shut down
capy::task<> test_after_shutdown()
{
    auto conn = co_await establish_connection();
    if (!check_success(co_await conn.shutdown()))
        co_return;
    BOOST_TEST_NOT(conn.has_ops_in_flight());
}

//
// Session establishment and teardown
//

// connect() counts while it runs
capy::task<> test_connect()
{
    co_connection conn{co_await capy::this_coro::executor};
    diagnostics diag;

    static_cast<void>(co_await capy::when_all(
        [&]() -> capy::task<capy::results<>> {
            check_success(co_await conn.connect(default_connect_params(), &diag), diag);
            co_return {};
        }(),

        [&]() -> capy::task<capy::results<>> {
            BOOST_TEST(conn.has_ops_in_flight());
            co_return {};
        }()
    ));

    BOOST_TEST_NOT(conn.has_ops_in_flight());
}

// So does shutdown()
capy::task<> test_shutdown()
{
    auto conn = co_await establish_connection();

    static_cast<void>(co_await capy::when_all(
        [&]() -> capy::task<capy::results<>> {
            check_success(co_await conn.shutdown());
            co_return {};
        }(),

        [&]() -> capy::task<capy::results<>> {
            BOOST_TEST(conn.has_ops_in_flight());
            co_return {};
        }()
    ));

    BOOST_TEST_NOT(conn.is_connected());
    BOOST_TEST_NOT(conn.has_ops_in_flight());
}

//
// High-level operations
//

// exec() counts while it runs. The response handler is invoked from inside the
// operation, which makes this the one observation point that needs no
// concurrent sibling
capy::task<> test_exec()
{
    auto conn = co_await establish_connection();

    bool observed = false;
    auto cb = [&](row_int) { observed = conn.has_ops_in_flight(); };

    request req;
    req.add_query("SELECT $1 AS value", 42);
    if (!co_await checked_exec(conn, req, resultset_callback<row_int>(cb)))
        co_return;

    BOOST_TEST(observed);
    BOOST_TEST_NOT(conn.has_ops_in_flight());
}

// receive() counts while it waits for a notification
capy::task<> test_receive()
{
    auto conn = co_await establish_connection();

    if (!co_await checked_exec(conn, request().add_query("LISTEN \"test_ops_in_flight\"")))
        co_return;

    // Recall that when_all launches things in order, so the receive() below is
    // already waiting when the observer runs
    notification_vector notifs;
    static_cast<void>(co_await capy::when_all(
        [&]() -> capy::task<capy::results<>> {
            check_success(co_await conn.receive(notifs));
            co_return {};
        }(),

        [&]() -> capy::task<capy::results<>> {
            BOOST_TEST(conn.has_ops_in_flight());

            // Unblock the receive()
            co_await checked_exec(conn, request().add_query("NOTIFY test_ops_in_flight"));
            co_return {};
        }()
    ));

    BOOST_TEST_NOT(conn.has_ops_in_flight());
    co_await check_connection_usable(conn);
}

// copy_in() counts while it runs
capy::task<> test_copy_in()
{
    auto conn = co_await establish_connection();

    if (!co_await checked_exec(conn, "CREATE TEMPORARY TABLE copy_in_test (id INT, name TEXT)"))
        co_return;

    request req;
    req.add_query("COPY copy_in_test FROM STDIN");

    capy::test::buffer_source source{{}, 8u};  // max 8 bytes per pull
    source.provide("1\tone\n2\ttwo\n");
    diagnostics diag;

    // Recall that when_all launches things in order
    static_cast<void>(co_await capy::when_all(
        [&]() -> capy::task<capy::results<>> {
            auto [ec, rows] = co_await conn.copy_in(req, &source, &diag);
            if (check_success(ec, diag))
                BOOST_TEST_EQ(rows, 2u);
            co_return {};
        }(),

        [&]() -> capy::task<capy::results<>> {
            BOOST_TEST(conn.has_ops_in_flight());
            co_return {};
        }()
    ));

    BOOST_TEST_NOT(conn.has_ops_in_flight());
    co_await check_connection_usable(conn);
}

// copy_out() counts while it runs
capy::task<> test_copy_out()
{
    auto conn = co_await establish_connection();

    request req_setup;
    req_setup.add_query("CREATE TEMPORARY TABLE copy_out_test (id INT, name TEXT)");
    req_setup.add_query("INSERT INTO copy_out_test VALUES (1, 'one'), (2, 'two')");
    if (!co_await checked_exec(conn, req_setup))
        co_return;

    request req;
    req.add_query("COPY copy_out_test TO STDOUT");

    capy::test::write_sink sink;
    diagnostics diag;

    // Recall that when_all launches things in order
    static_cast<void>(co_await capy::when_all(
        [&]() -> capy::task<capy::results<>> {
            auto [ec, rows] = co_await conn.copy_out(req, &sink, &diag);
            if (check_success(ec, diag))
                BOOST_TEST_EQ(rows, 2u);
            co_return {};
        }(),

        [&]() -> capy::task<capy::results<>> {
            BOOST_TEST(conn.has_ops_in_flight());
            co_return {};
        }()
    ));

    BOOST_TEST_NOT(conn.has_ops_in_flight());
    co_await check_connection_usable(conn);
}

//
// Low-level operations
//

// write_request() counts while it runs. prepare_request() is synchronous and
// doesn't start anything, so it must not count
capy::task<> test_write_request()
{
    auto conn = co_await establish_connection();

    request req;
    req.add_query("SELECT $1 AS value", 42);
    check handler;

    exec_state st;
    if (!BOOST_TEST_EQ(conn.prepare_request(st, req, &handler), std::error_code()))
        co_return;
    BOOST_TEST_NOT(conn.has_ops_in_flight());

    // Recall that when_all launches things in order
    static_cast<void>(co_await capy::when_all(
        [&]() -> capy::task<capy::results<>> {
            check_success(co_await conn.write_request(st));
            co_return {};
        }(),

        [&]() -> capy::task<capy::results<>> {
            BOOST_TEST(conn.has_ops_in_flight());
            co_return {};
        }()
    ));

    BOOST_TEST_NOT(conn.has_ops_in_flight());

    // We wrote a request we never read. Abandon it and let the next exec
    // discard its response
    st.reset();
    co_await check_connection_usable(conn);
}

// read_some_response() counts while it runs
capy::task<> test_read_some_response()
{
    auto conn = co_await establish_connection();

    request req;
    req.add_query("SELECT $1 AS value", 42);
    check handler;

    exec_state st;
    if (!BOOST_TEST_EQ(conn.prepare_request(st, req, &handler), std::error_code()))
        co_return;
    if (!check_success(co_await conn.write_request(st)))
        co_return;
    BOOST_TEST_NOT(conn.has_ops_in_flight());

    // Recall that when_all launches things in order
    static_cast<void>(co_await capy::when_all(
        [&]() -> capy::task<capy::results<>> {
            check_success(co_await read_until_done(conn, st));
            co_return {};
        }(),

        [&]() -> capy::task<capy::results<>> {
            BOOST_TEST(conn.has_ops_in_flight());
            co_return {};
        }()
    ));

    BOOST_TEST_NOT(conn.has_ops_in_flight());
    check_success(st.handler_error());
    co_await check_connection_usable(conn);
}

// write_some_copy_data() counts while it runs
capy::task<> test_write_some_copy_data()
{
    auto conn = co_await establish_connection();

    if (!co_await checked_exec(conn, "CREATE TEMPORARY TABLE copy_in_test (id INT, name TEXT)"))
        co_return;

    request req;
    req.add_query("COPY copy_in_test FROM STDIN");
    check handler;

    exec_state st;
    if (!check_success(co_await enter_copy_data(conn, st, req, &handler)))
        co_return;
    BOOST_TEST_NOT(conn.has_ops_in_flight());

    // Recall that when_all launches things in order
    constexpr auto data = "1\tone\n"sv;
    static_cast<void>(co_await capy::when_all(
        [&]() -> capy::task<capy::results<>> {
            // A single call may transfer only part of the buffer, and the copy
            // can't be terminated until all of it has been handed over
            auto remaining = data;
            while (!remaining.empty())
            {
                auto [ec, bytes] = co_await conn.write_some_copy_data(st, capy::make_buffer(remaining));
                if (!check_success(ec))
                    break;
                remaining = remaining.substr(bytes);
            }
            co_return {};
        }(),

        [&]() -> capy::task<capy::results<>> {
            BOOST_TEST(conn.has_ops_in_flight());
            co_return {};
        }()
    ));

    BOOST_TEST_NOT(conn.has_ops_in_flight());

    // Terminate the copy, so the connection is left usable
    if (!check_success(co_await conn.write_copy_done(st)))
        co_return;
    if (!check_success(co_await read_until_done(conn, st)))
        co_return;
    check_success(st.handler_error());
    co_await check_connection_usable(conn);
}

// write_copy_done() counts while it runs
capy::task<> test_write_copy_done()
{
    auto conn = co_await establish_connection();

    if (!co_await checked_exec(conn, "CREATE TEMPORARY TABLE copy_in_test (id INT, name TEXT)"))
        co_return;

    request req;
    req.add_query("COPY copy_in_test FROM STDIN");
    check handler;

    exec_state st;
    if (!check_success(co_await enter_copy_data(conn, st, req, &handler)))
        co_return;

    // Terminating an empty copy is valid, so there is no data to send first
    BOOST_TEST_NOT(conn.has_ops_in_flight());

    // Recall that when_all launches things in order
    static_cast<void>(co_await capy::when_all(
        [&]() -> capy::task<capy::results<>> {
            check_success(co_await conn.write_copy_done(st));
            co_return {};
        }(),

        [&]() -> capy::task<capy::results<>> {
            BOOST_TEST(conn.has_ops_in_flight());
            co_return {};
        }()
    ));

    BOOST_TEST_NOT(conn.has_ops_in_flight());

    if (!check_success(co_await read_until_done(conn, st)))
        co_return;
    check_success(st.handler_error());
    co_await check_connection_usable(conn);
}

// write_copy_fail() counts while it runs
capy::task<> test_write_copy_fail()
{
    auto conn = co_await establish_connection();

    if (!co_await checked_exec(conn, "CREATE TEMPORARY TABLE copy_in_test (id INT, name TEXT)"))
        co_return;

    request req;
    req.add_query("COPY copy_in_test FROM STDIN");
    check handler;

    exec_state st;
    if (!check_success(co_await enter_copy_data(conn, st, req, &handler)))
        co_return;
    BOOST_TEST_NOT(conn.has_ops_in_flight());

    // Recall that when_all launches things in order
    static_cast<void>(co_await capy::when_all(
        [&]() -> capy::task<capy::results<>> {
            check_success(co_await conn.write_copy_fail(st, "aborted by test"));
            co_return {};
        }(),

        [&]() -> capy::task<capy::results<>> {
            BOOST_TEST(conn.has_ops_in_flight());
            co_return {};
        }()
    ));

    BOOST_TEST_NOT(conn.has_ops_in_flight());

    // The server reports the copy as failed. We only care that reading the
    // response leaves the connection usable - test_copy_fail covers the error
    if (!check_success(co_await read_until_done(conn, st)))
        co_return;
    BOOST_TEST_NE(st.handler_error().code, std::error_code());
    co_await check_connection_usable(conn);
}

}  // namespace

int main()
{
    run_coroutine_test(test_never_connected());
    run_coroutine_test(test_idle_connection());
    run_coroutine_test(test_after_shutdown());

    run_coroutine_test(test_connect());
    run_coroutine_test(test_shutdown());

    run_coroutine_test(test_exec());
    run_coroutine_test(test_receive());
    run_coroutine_test(test_copy_in());
    run_coroutine_test(test_copy_out());

    run_coroutine_test(test_write_request());
    run_coroutine_test(test_read_some_response());
    run_coroutine_test(test_write_some_copy_data());
    run_coroutine_test(test_write_copy_done());
    run_coroutine_test(test_write_copy_fail());

    return boost::report_errors();
}
