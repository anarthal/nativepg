//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/capy/buffers.hpp>
#include <boost/capy/buffers/buffer_copy.hpp>
#include <boost/capy/buffers/make_buffer.hpp>
#include <boost/capy/concept/const_buffer_sequence.hpp>
#include <boost/capy/concept/write_sink.hpp>
#include <boost/capy/delay.hpp>
#include <boost/capy/io_task.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/test/write_sink.hpp>
#include <boost/capy/when_all.hpp>
#include <boost/core/lightweight_test.hpp>
#include <boost/describe/class.hpp>
#include <boost/describe/operators.hpp>

#include <cstddef>
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
#include "test_utils/yield.hpp"

// Covers co_connection::copy_out

namespace capy = boost::capy;
using namespace nativepg;
using namespace nativepg::test;
using namespace std::string_view_literals;
using namespace std::chrono_literals;

namespace {

struct row_string
{
    std::string value;
};
BOOST_DESCRIBE_STRUCT(row_string, (), (value))

using boost::describe::operators::operator==;
using boost::describe::operators::operator<<;

constexpr auto create_table_sql = "CREATE TEMPORARY TABLE copy_out_test (id INT, name TEXT)"sv;
constexpr auto insert_rows_sql = "INSERT INTO copy_out_test VALUES (1, 'one'), (2, 'two'), (3, 'three')"sv;

// The contents that setup_table writes, as the server renders them in COPY's
// default text format: tab-separated columns, newline-separated rows
constexpr std::string_view expected_copy_data = "1\tone\n2\ttwo\n3\tthree\n"sv;

// A Copy-out producing data works
capy::task<> test_success()
{
    // Setup
    auto conn = co_await establish_connection();

    request req_setup;
    req_setup.add_query(create_table_sql);
    req_setup.add_query(insert_rows_sql);
    if (!co_await checked_exec(conn, req_setup))
        co_return;

    request req;
    req.add_query("COPY copy_out_test TO STDOUT");

    capy::test::write_sink sink;
    diagnostics diag;

    // Run the copy
    auto [ec, rows] = co_await conn.copy_out(req, &sink, &diag);
    if (!check_success(ec, diag))
        co_return;

    // Check
    BOOST_TEST_EQ(rows, 3u);
    BOOST_TEST_EQ(sink.data(), "1\tone\n2\ttwo\n3\tthree\n"sv);
    BOOST_TEST(sink.eof_called());

    co_await check_connection_usable(conn);
}

// A Copy-out producing no rows also works
capy::task<> test_no_data()
{
    // Setup
    auto conn = co_await establish_connection();

    if (!co_await checked_exec(conn, create_table_sql))
        co_return;

    request req;
    req.add_query("COPY copy_out_test TO STDOUT");

    capy::test::write_sink sink;
    diagnostics diag;

    // Run the copy
    auto [ec, rows] = co_await conn.copy_out(req, &sink, &diag);
    if (!check_success(ec, diag))
        co_return;

    // The copy succeeded, and copied nothing
    BOOST_TEST_EQ(rows, 0u);
    BOOST_TEST_EQ(sink.data(), std::string_view());
    BOOST_TEST(sink.eof_called());

    co_await check_connection_usable(conn);
}

// The request may contain non-copy commands before/after the copy
capy::task<> test_request_with_other_commands()
{
    // Setup
    auto conn = co_await establish_connection();

    request req_setup;
    req_setup.add_query(create_table_sql);
    req_setup.add_query(insert_rows_sql);
    if (!co_await checked_exec(conn, req_setup))
        co_return;

    // The copy is pipelined between two statements that produce regular resultsets
    request req;
    req.add_query("SELECT 'before' AS value");
    req.add_query("COPY copy_out_test TO STDOUT");
    req.add_query("SELECT 'after' AS value");

    capy::test::write_sink sink;

    diagnostics diag;
    auto [ec, rows] = co_await conn.copy_out(req, &sink, &diag);
    if (!check_success(ec, diag))
        co_return;

    // Check
    BOOST_TEST_EQ(rows, 3u);  // not affected by the surrounding statements
    BOOST_TEST_EQ(sink.data(), expected_copy_data);
    BOOST_TEST(sink.eof_called());

    co_await check_connection_usable(conn);
}

// A sink that returns to the event loop before consuming the buffers it is given
class yielding_sink
{
    capy::test::write_sink sink_;

public:
    capy::test::write_sink& get() { return sink_; }

    template <capy::ConstBufferSequence ConstBufferSeq>
    capy::io_task<std::size_t> write(ConstBufferSeq buffers)
    {
        static_cast<void>(co_await capy::delay(1ms));
        co_return co_await sink_.write(std::move(buffers));
    }

    template <capy::ConstBufferSequence ConstBufferSeq>
    capy::io_task<std::size_t> write_some(ConstBufferSeq buffers)
    {
        static_cast<void>(co_await capy::delay(1ms));
        co_return co_await sink_.write_some(std::move(buffers));
    }

    template <capy::ConstBufferSequence ConstBufferSeq>
    capy::io_task<std::size_t> write_eof(ConstBufferSeq buffers)
    {
        static_cast<void>(co_await capy::delay(1ms));
        co_return co_await sink_.write_eof(std::move(buffers));
    }

    capy::io_task<> write_eof()
    {
        static_cast<void>(co_await capy::delay(1ms));
        co_return co_await sink_.write_eof();
    }
};
static_assert(capy::WriteSink<yielding_sink>);

// Regression test: requests multiplexed after a Copy-out don't cause use-after-free.
// This can happen because sinks are asynchronous. The multiplexer allows subsequent readers
// immediately after the last RFQ is read. If we are not careful, the next read operation
// might invalidate data that the sink hasn't consumed yet.
capy::task<> test_multiplexing()
{
    // Setup
    auto conn = co_await establish_connection();

    request req_setup;
    req_setup.add_query(create_table_sql);
    req_setup.add_query(insert_rows_sql);
    if (!co_await checked_exec(conn, req_setup))
        co_return;

    // Both requests are handed to the multiplexer before either completes
    static_cast<void>(co_await capy::when_all(
        // Run the Copy-out
        [&]() -> capy::io_task<> {
            request req;
            req.add_query("COPY copy_out_test TO STDOUT");

            yielding_sink sink;
            diagnostics diag;

            auto [ec, rows] = co_await conn.copy_out(req, &sink, &diag);
            if (!check_success(ec, diag))
                co_return {};

            // The copy data survived the second request reading over the buffer it lived in
            BOOST_TEST_EQ(rows, 3u);
            BOOST_TEST_EQ(sink.get().data(), expected_copy_data);
            BOOST_TEST(sink.get().eof_called());

            co_return {};
        }(),

        // Run the other request
        [&]() -> capy::io_task<> {
            // Ensure we go after the copy
            co_await yield();

            // A single row far bigger than the read buffer, so that reading it
            // reallocates the buffer rather than reusing it
            constexpr std::size_t big_value_size = 4096u * 8u;
            request req;
            req.add_query("SELECT repeat('x', $1::int) AS value", static_cast<int>(big_value_size));

            std::vector<row_string> strings;

            if (!co_await checked_exec(conn, req, into(strings)))
                co_return {};

            const row_string expected[] = {{.value = std::string(big_value_size, 'x')}};
            test_range_eq(strings, expected);

            co_return {};
        }()
    ));

    co_await check_connection_usable(conn);
}

// The server rejects the request, so it never enters copy-out mode
capy::task<> test_server_error()
{
    // Setup
    auto conn = co_await establish_connection();

    // The table doesn't exist
    request req;
    req.add_query("COPY copy_out_does_not_exist TO STDOUT");

    capy::test::write_sink sink;
    diagnostics diag;

    // Run the copy
    auto [ec, rows] = co_await conn.copy_out(req, &sink, &diag);

    // The server's error is reported
    test_cond_eq(ec, sqlstate_cond::undefined_table);
    test_string_contains(diag.message(), "copy_out_does_not_exist");

    // The copy never started, and a failed transfer doesn't finalize the sink
    BOOST_TEST_EQ(rows, 0u);
    BOOST_TEST_EQ(sink.data(), std::string_view());
    BOOST_TEST_NOT(sink.eof_called());

    // A rejected request is not a protocol error
    co_await check_connection_usable(conn);
}

// The request runs successfully, but never makes the server enter copy-out mode
capy::task<> test_zero_copies()
{
    // Setup
    auto conn = co_await establish_connection();

    // A plain query produces a regular resultset, rather than a copy
    request req;
    req.add_query("SELECT 'value' AS value");

    capy::test::write_sink sink;
    diagnostics diag;

    // Run the copy
    auto [ec, rows] = co_await conn.copy_out(req, &sink, &diag);

    // The request succeeded, but it's not doing what the user wants
    BOOST_TEST_EQ(ec, std::error_code(client_errc::incompatible_response_type));
    BOOST_TEST(diag.message().empty());

    // Nothing was ever transferred
    BOOST_TEST_EQ(rows, 0u);
    BOOST_TEST_EQ(sink.data(), std::string_view());
    BOOST_TEST_NOT(sink.eof_called());

    co_await check_connection_usable(conn);
}

// The request makes the server enter copy-out mode more than once
capy::task<> test_two_copies()
{
    // Setup
    auto conn = co_await establish_connection();

    request req_setup;
    req_setup.add_query(create_table_sql);
    req_setup.add_query(insert_rows_sql);
    if (!co_await checked_exec(conn, req_setup))
        co_return;

    // A single simple query may contain several statements
    request req;
    req.add_query("COPY copy_out_test TO STDOUT");
    req.add_query("COPY copy_out_test TO STDOUT");

    capy::test::write_sink sink;
    diagnostics diag;

    // Run the copy
    auto [ec, rows] = co_await conn.copy_out(req, &sink, &diag);

    // The request succeeded, but it's not doing what the user wants
    BOOST_TEST_EQ(ec, std::error_code(client_errc::incompatible_response_type));
    BOOST_TEST_EQ(rows, 0u);

    // The transfer never completed. How much of the first copy reached the sink is unspecified
    BOOST_TEST_NOT(sink.eof_called());

    co_await check_connection_usable(conn);
}

// The error reported by failing_sink
const std::error_code sink_error = std::make_error_code(std::errc::io_error);

// A WriteSink that fails on its first write.
// fuse does not allow injecting individual errors
class failing_sink
{
    capy::test::write_sink sink_;

public:
    capy::test::write_sink& get() { return sink_; }

    template <capy::ConstBufferSequence ConstBufferSeq>
    capy::io_task<std::size_t> write(ConstBufferSeq)
    {
        co_return {sink_error, 0u};
    }

    template <capy::ConstBufferSequence ConstBufferSeq>
    capy::io_task<std::size_t> write_some(ConstBufferSeq buffers)
    {
        return write(std::move(buffers));
    }

    template <capy::ConstBufferSequence ConstBufferSeq>
    capy::io_task<std::size_t> write_eof(ConstBufferSeq buffers)
    {
        return write(std::move(buffers));
    }

    capy::io_task<> write_eof() { co_return co_await sink_.write_eof(); }
};
static_assert(capy::WriteSink<failing_sink>);

// The sink fails while the server is sending us data
capy::task<> test_sink_error()
{
    // Setup
    auto conn = co_await establish_connection();

    // Big enough that the response spans several reads, so that failing on the
    // first write leaves part unread
    request req;
    req.add_query("COPY (SELECT i, repeat('x', 100) FROM generate_series(1, 5000) i) TO STDOUT");

    failing_sink sink;
    diagnostics diag;

    // Run the copy
    auto [ec, rows] = co_await conn.copy_out(req, &sink, &diag);

    // The sink's error is reported
    BOOST_TEST_EQ(ec, sink_error);
    BOOST_TEST(diag.message().empty());

    // A failed transfer doesn't finalize the sink
    BOOST_TEST_EQ(rows, 0u);
    BOOST_TEST_NOT(sink.get().eof_called());

    co_await check_connection_usable(conn);
}

}  // namespace

int main()
{
    run_coroutine_test(test_success());
    run_coroutine_test(test_no_data());
    run_coroutine_test(test_request_with_other_commands());
    run_coroutine_test(test_multiplexing());

    run_coroutine_test(test_server_error());
    run_coroutine_test(test_zero_copies());
    run_coroutine_test(test_two_copies());
    run_coroutine_test(test_sink_error());

    return boost::report_errors();
}
