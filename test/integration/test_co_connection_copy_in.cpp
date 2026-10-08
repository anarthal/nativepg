//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/assert.hpp>
#include <boost/assert/source_location.hpp>
#include <boost/capy/buffers.hpp>
#include <boost/capy/buffers/make_buffer.hpp>
#include <boost/capy/cond.hpp>
#include <boost/capy/delay.hpp>
#include <boost/capy/error.hpp>
#include <boost/capy/io_task.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/test/buffer_source.hpp>
#include <boost/core/lightweight_test.hpp>
#include <boost/describe/class.hpp>
#include <boost/describe/operators.hpp>

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "nativepg/client_errc.hpp"
#include "nativepg/co_connection.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/request.hpp"
#include "nativepg/responses/into.hpp"
#include "nativepg/sqlstate_cond.hpp"
#include "test_utils/co_connection_utils.hpp"
#include "test_utils/corosio_utils.hpp"
#include "test_utils/printing.hpp"
#include "test_utils/test_cond_eq.hpp"
#include "test_utils/test_range_eq.hpp"
#include "test_utils/test_string_contains.hpp"

// Covers co_connection::copy_in

namespace capy = boost::capy;
using namespace nativepg;
using namespace nativepg::test;
using namespace std::string_view_literals;
using namespace std::chrono_literals;

namespace {

struct row_copy
{
    int id;
    std::string name;
};
BOOST_DESCRIBE_STRUCT(row_copy, (), (id, name))

using boost::describe::operators::operator==;
using boost::describe::operators::operator<<;

// The error reported by failing_source
const std::error_code source_error = std::make_error_code(std::errc::io_error);

// TODO: duplicated
capy::task<> check_copy_rows(
    co_connection& conn,
    std::span<const row_copy> expected,
    boost::source_location loc = BOOST_CURRENT_LOCATION
)
{
    std::vector<row_copy> actual;
    request req_check;
    req_check.add_query("SELECT id, name FROM copy_in_test ORDER BY id");
    if (!co_await checked_exec(conn, req_check, into(actual), loc))
        co_return;
    test_range_eq(actual, expected, loc);
}

// The source is drained in several separate pull() calls,
// and all the rows reach the server
capy::task<> test_success()
{
    // Setup
    auto conn = co_await establish_connection();

    if (!co_await checked_exec(conn, "CREATE TEMPORARY TABLE copy_in_test (id INT, name TEXT)"))
        co_return;

    // Run the copy
    request req;
    req.add_query("COPY copy_in_test FROM STDIN");

    capy::test::buffer_source source{{}, 8u};  // max 8 bytes per pull
    source.provide("1\tone\n2\ttwo\n3\tthree\n");

    diagnostics diag;
    auto [ec, rows] = co_await conn.copy_in(req, &source, &diag);
    if (!check_success(ec, diag))
        co_return;

    // The server reports how many rows it copied
    BOOST_TEST_EQ(rows, 3u);

    // The rows made it into the table
    const row_copy expected[] = {
        {.id = 1, .name = "one"  },
        {.id = 2, .name = "two"  },
        {.id = 3, .name = "three"},
    };
    co_await check_copy_rows(conn, expected);

    co_await check_connection_usable(conn);
}

// The source is empty
capy::task<> test_success_no_data()
{
    // Setup
    auto conn = co_await establish_connection();

    if (!co_await checked_exec(conn, "CREATE TEMPORARY TABLE copy_in_test (id INT, name TEXT)"))
        co_return;

    // Run the copy
    request req;
    req.add_query("COPY copy_in_test FROM STDIN");

    capy::test::buffer_source source;

    diagnostics diag;
    auto [ec, rows] = co_await conn.copy_in(req, &source, &diag);
    if (!check_success(ec, diag))
        co_return;

    // The server reports how many rows it copied
    BOOST_TEST_EQ(rows, 0u);

    // No rows were copied
    co_await check_copy_rows(conn, {});

    co_await check_connection_usable(conn);
}

// The server rejects the request, so it never asks us for data
capy::task<> test_server_rejects_copy()
{
    // Setup
    auto conn = co_await establish_connection();

    // The target table doesn't exist
    request req;
    req.add_query("COPY copy_in_nonexistent FROM STDIN");

    capy::test::buffer_source source;
    source.provide("1\tone\n");

    diagnostics diag;
    auto [ec, rows] = co_await conn.copy_in(req, &source, &diag);

    // The server's error is reported
    test_cond_eq(ec, sqlstate_cond::undefined_table);
    test_string_contains(diag.message(), "copy_in_nonexistent");

    // We never got asked for data, so the source wasn't touched
    BOOST_TEST_EQ(source.available(), 6u);

    // A rejected request is not a protocol error
    co_await check_connection_usable(conn);
}

// The server rejects our data while we're sending it
capy::task<> test_server_error_mid_copy()
{
    // Ensure that we do not keep sending data once we receive an error.
    // This source keeps spitting data forever.
    // If we don't stop pulling, the test will fail by timeout.
    class infinite_source
    {
        capy::test::buffer_source source_;

    public:
        infinite_source()
        {
            source_.provide("1\tone\n2\ttwo\tthree\n");  // Malformed row
        }

        capy::io_task<std::span<capy::const_buffer>> pull(std::span<capy::const_buffer> dest)
        {
            while (true)
            {
                // If we have something, return it
                auto [ec, res] = co_await source_.pull(dest);
                if (ec != capy::cond::eof)
                    co_return {ec, res};

                // Sleep for a brief period of time, to return to the event loop.
                // yield does not seem to work here
                static_cast<void>(co_await capy::delay(1ms));

                // Replenish the data
                source_.provide("more_data");
            }
        }

        void consume(std::size_t n) noexcept { source_.consume(n); }
    };

    // Setup
    auto conn = co_await establish_connection();

    if (!co_await checked_exec(conn, "CREATE TEMPORARY TABLE copy_in_test (id INT, name TEXT)"))
        co_return;

    request req;
    req.add_query("COPY copy_in_test FROM STDIN");
    infinite_source source;

    diagnostics diag;
    auto [ec, rows] = co_await conn.copy_in(req, &source, &diag);

    // The server's error is reported
    test_cond_eq(ec, sqlstate_cond::bad_copy_file_format);
    BOOST_TEST_NOT(diag.message().empty());

    // No data was copied
    co_await check_copy_rows(conn, {});

    // A rejected copy is not a protocol error
    co_await check_connection_usable(conn);
}

// The request runs successfully, but never asks us for data
capy::task<> test_request_without_copies()
{
    // Setup
    auto conn = co_await establish_connection();

    request req;
    req.add_query("SELECT 1");

    capy::test::buffer_source source;
    source.provide("1\tone\n");

    diagnostics diag;
    auto [ec, rows] = co_await conn.copy_in(req, &source, &diag);

    BOOST_TEST_EQ(ec, std::error_code(client_errc::invalid_copy_in_request));
    BOOST_TEST(diag.message().empty());

    // We never got asked for data, so the source wasn't touched
    BOOST_TEST_EQ(source.available(), 6u);

    co_await check_connection_usable(conn);
}

// The request asks us for data more than once. We only know how to feed one copy
capy::task<> test_request_with_two_copies()
{
    // Setup
    auto conn = co_await establish_connection();

    if (!co_await checked_exec(conn, "CREATE TEMPORARY TABLE copy_in_test (id INT, name TEXT)"))
        co_return;

    request req;
    req.add_simple_query(
        "COPY copy_in_test FROM STDIN;"
        "COPY copy_in_test FROM STDIN"
    );

    capy::test::buffer_source source;
    source.provide("1\tone\n");

    diagnostics diag;
    auto [ec, rows] = co_await conn.copy_in(req, &source, &diag);

    BOOST_TEST_EQ(ec, std::error_code(client_errc::invalid_copy_in_request));

    // The second copy is terminated with a CopyFail, which keeps the connection in sync
    co_await check_connection_usable(conn);
}

// The source fails while we're transferring data
capy::task<> test_source_error()
{
    // A BufferSource that delivers data for a number of pulls, and then fails
    // fuse does not allow injecting individual errors
    class failing_source
    {
        capy::test::buffer_source source_{{}, 8u};
        int remaining_pulls_{1};

    public:
        failing_source() = default;

        capy::io_task<std::span<capy::const_buffer>> pull(std::span<capy::const_buffer> dest)
        {
            if (--remaining_pulls_ == 0)
                co_return {source_error, {}};
            co_return co_await source_.pull(dest);
        }

        void consume(std::size_t n) noexcept { source_.consume(n); }
    };

    // Setup
    auto conn = co_await establish_connection();

    if (!co_await checked_exec(conn, "CREATE TEMPORARY TABLE copy_in_test (id INT, name TEXT)"))
        co_return;

    request req;
    req.add_query("COPY copy_in_test FROM STDIN");

    // The first pull succeeds, the second one fails
    failing_source source;

    diagnostics diag;
    auto [ec, rows] = co_await conn.copy_in(req, &source, &diag);

    // The source's error is reported. The error that our CopyFail triggered
    // in the server is an echo of this one, so it's not reported instead
    BOOST_TEST_EQ(ec, source_error);
    BOOST_TEST(diag.message().empty());

    // The copy was aborted, so nothing was committed
    co_await check_copy_rows(conn, {});

    // Aborting a copy is not a protocol error
    co_await check_connection_usable(conn);
}

}  // namespace

int main()
{
    run_coroutine_test(test_success());
    run_coroutine_test(test_success_no_data());

    run_coroutine_test(test_server_rejects_copy());
    run_coroutine_test(test_server_error_mid_copy());
    run_coroutine_test(test_request_without_copies());
    run_coroutine_test(test_request_with_two_copies());
    run_coroutine_test(test_source_error());

    return boost::report_errors();
}
