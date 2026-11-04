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

// Runs an operation until reaching Copy-in mode
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

capy::task<> test_connect()
{
    // Setup
    co_connection conn{co_await capy::this_coro::executor};
    diagnostics diag;

    // The function makes has_ops_in_flight() return true
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

    // And it's reset when it completes
    BOOST_TEST_NOT(conn.has_ops_in_flight());
}

capy::task<> test_shutdown()
{
    // Setup
    auto conn = co_await establish_connection();

    // The function makes has_ops_in_flight() return true
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

    // And it's reset when it completes
    BOOST_TEST_NOT(conn.has_ops_in_flight());
}

//
// High-level operations
//

capy::task<> test_exec()
{
    // Setup
    auto conn = co_await establish_connection();
    request req;
    req.add_query("SELECT 1");

    // The function makes has_ops_in_flight() return true
    static_cast<void>(co_await capy::when_all(
        [&]() -> capy::task<capy::results<>> {
            check_success(co_await conn.exec(req, check()));
            co_return {};
        }(),

        [&]() -> capy::task<capy::results<>> {
            BOOST_TEST(conn.has_ops_in_flight());
            co_return {};
        }()
    ));

    // And it's reset when it completes
    BOOST_TEST_NOT(conn.has_ops_in_flight());
}

capy::task<> test_receive()
{
    // Setup
    auto conn = co_await establish_connection();

    if (!co_await checked_exec(conn, "LISTEN test_ops_in_flight"))
        co_return;

    // The function makes has_ops_in_flight() return true
    static_cast<void>(co_await capy::when_all(
        [&]() -> capy::task<capy::results<>> {
            notification_vector notifs;
            check_success(co_await conn.receive(notifs));
            co_return {};
        }(),

        [&]() -> capy::task<capy::results<>> {
            BOOST_TEST(conn.has_ops_in_flight());

            // Unblock receive()
            co_await checked_exec(conn, "NOTIFY test_ops_in_flight");
            co_return {};
        }()
    ));

    // And it's reset when it completes
    BOOST_TEST_NOT(conn.has_ops_in_flight());
}

capy::task<> test_copy_in()
{
    // Setup
    auto conn = co_await establish_connection();

    if (!co_await checked_exec(conn, "CREATE TEMPORARY TABLE copy_in_test (id INT, name TEXT)"))
        co_return;

    request req;
    req.add_query("COPY copy_in_test FROM STDIN");

    capy::test::buffer_source source{{}, 8u};  // max 8 bytes per pull
    source.provide("1\tone\n2\ttwo\n");
    diagnostics diag;

    // The function makes has_ops_in_flight() return true
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

    // And it's reset when it completes
    BOOST_TEST_NOT(conn.has_ops_in_flight());
}

capy::task<> test_copy_out()
{
    // Setup
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

    // The function makes has_ops_in_flight() return true
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

    // And it's reset when it completes
    BOOST_TEST_NOT(conn.has_ops_in_flight());
}

//
// Low-level operations
//

capy::task<> test_write_request()
{
    // Setup
    auto conn = co_await establish_connection();

    request req;
    req.add_query("SELECT $1 AS value", 42);
    check handler;

    // Prepare a request. This does not count because it's sync
    exec_state st;
    if (!BOOST_TEST_EQ(conn.prepare_request(st, req, &handler), std::error_code()))
        co_return;
    BOOST_TEST_NOT(conn.has_ops_in_flight());

    // The function makes has_ops_in_flight() return true
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

    // And it's reset when it completes
    BOOST_TEST_NOT(conn.has_ops_in_flight());
}

capy::task<> test_read_some_response()
{
    // Setup
    auto conn = co_await establish_connection();

    request req;
    req.add_query("SELECT $1 AS value", 42);
    check handler;

    // Prepare a request and write it
    exec_state st;
    if (!BOOST_TEST_EQ(conn.prepare_request(st, req, &handler), std::error_code()))
        co_return;
    if (!check_success(co_await conn.write_request(st)))
        co_return;
    BOOST_TEST_NOT(conn.has_ops_in_flight());

    // The function makes has_ops_in_flight() return true
    static_cast<void>(co_await capy::when_all(
        [&]() -> capy::task<capy::results<>> {
            check_success(co_await conn.read_some_response(st));
            co_return {};
        }(),

        [&]() -> capy::task<capy::results<>> {
            BOOST_TEST(conn.has_ops_in_flight());
            co_return {};
        }()
    ));

    // And it's reset when it completes
    BOOST_TEST_NOT(conn.has_ops_in_flight());
}

capy::task<> test_write_some_copy_data()
{
    // Setup
    auto conn = co_await establish_connection();

    if (!co_await checked_exec(conn, "CREATE TEMPORARY TABLE copy_in_test (id INT, name TEXT)"))
        co_return;

    request req;
    req.add_query("COPY copy_in_test FROM STDIN");
    check handler;

    // Run the request until entering Copy-in
    exec_state st;
    if (!check_success(co_await enter_copy_data(conn, st, req, &handler)))
        co_return;
    BOOST_TEST_NOT(conn.has_ops_in_flight());

    // The function makes has_ops_in_flight() return true
    static_cast<void>(co_await capy::when_all(
        [&]() -> capy::task<capy::results<>> {
            auto [ec, bytes] = co_await conn.write_some_copy_data(st, capy::make_buffer("1\tone\n"sv));
            check_success(ec);
            co_return {};
        }(),

        [&]() -> capy::task<capy::results<>> {
            BOOST_TEST(conn.has_ops_in_flight());
            co_return {};
        }()
    ));

    // And it's reset when it completes
    BOOST_TEST_NOT(conn.has_ops_in_flight());
}

capy::task<> test_write_copy_done()
{
    // Setup
    auto conn = co_await establish_connection();

    if (!co_await checked_exec(conn, "CREATE TEMPORARY TABLE copy_in_test (id INT, name TEXT)"))
        co_return;

    request req;
    req.add_query("COPY copy_in_test FROM STDIN");
    check handler;

    // Run the request until entering Copy-in
    exec_state st;
    if (!check_success(co_await enter_copy_data(conn, st, req, &handler)))
        co_return;
    BOOST_TEST_NOT(conn.has_ops_in_flight());

    // The function makes has_ops_in_flight() return true
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

    // And it's reset when it completes
    BOOST_TEST_NOT(conn.has_ops_in_flight());
}

capy::task<> test_write_copy_fail()
{
    // Setup
    auto conn = co_await establish_connection();

    if (!co_await checked_exec(conn, "CREATE TEMPORARY TABLE copy_in_test (id INT, name TEXT)"))
        co_return;

    request req;
    req.add_query("COPY copy_in_test FROM STDIN");
    check handler;

    // Run the request until entering Copy-in
    exec_state st;
    if (!check_success(co_await enter_copy_data(conn, st, req, &handler)))
        co_return;
    BOOST_TEST_NOT(conn.has_ops_in_flight());

    // The function makes has_ops_in_flight() return true
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

    // And it's reset when it completes
    BOOST_TEST_NOT(conn.has_ops_in_flight());
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
