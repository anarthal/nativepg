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
#include <cstdint>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

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
capy::task<> test_success_no_data()
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

struct copy_out_result
{
    extended_error err;
    std::uint64_t rows{};
};

// Runs a copy_out and records its outcome, always completing successfully.
// Prevents when_all from cancelling the sibling request on error
capy::io_task<copy_out_result> do_copy_out(co_connection& conn, const request& req, yielding_sink& sink)
{
    copy_out_result out;
    auto [ec, rows] = co_await conn.copy_out(req, &sink, &out.err.diag);
    out.err.code = ec;
    out.rows = rows;
    co_return {{}, out};
}

// Runs a single exec and records its outcome, always completing successfully
template <response_handler Handler>
capy::io_task<extended_error> do_exec(co_connection& conn, const request& req, Handler handler)
{
    extended_error out;
    auto [ec] = co_await conn.exec(req, &handler, &out.diag);
    out.code = ec;
    co_return {{}, out};
}

// A request issued immediately after a copy-out is multiplexed with it, and each
// operation sees only its own response.
// This is a regression test: copy_out collects the copy data as views into the
// connection's read buffer, but read_some_response releases the reader turn
// before returning for the batch that completes the response. The second request
// therefore gets to read while the sink is still suspended holding those views,
// and its response is big enough to force the read buffer to be reallocated,
// which frees the memory the views point into
capy::task<> test_multiplexing()
{
    // Setup
    auto conn = co_await establish_connection();

    request req_setup;
    req_setup.add_query(create_table_sql);
    req_setup.add_query(insert_rows_sql);
    if (!co_await checked_exec(conn, req_setup))
        co_return;

    request copy_req;
    copy_req.add_query("COPY copy_out_test TO STDOUT");

    // A single row far bigger than the read buffer, so that reading it
    // reallocates the buffer rather than reusing it
    constexpr std::size_t big_value_size = 4096u * 2u;
    request other_req;
    other_req.add_query("SELECT repeat('x', $1::int) AS value", static_cast<int>(big_value_size));
    std::vector<row_string> strings;

    yielding_sink sink;

    // Both requests are handed to the multiplexer before either completes
    auto [dummy, copy_res, other_res] = co_await capy::when_all(
        do_copy_out(conn, copy_req, sink),
        do_exec(conn, other_req, into(strings))
    );

    // Both complete successfully
    if (!check_success(copy_res.err) || !check_success(other_res))
        co_return;

    // The copy data survived the second request reading over the buffer it lived in
    BOOST_TEST_EQ(copy_res.rows, 3u);
    BOOST_TEST_EQ(sink.get().data(), expected_copy_data);
    BOOST_TEST(sink.get().eof_called());

    // The second request got its own response
    const row_string expected[] = {{.value = std::string(big_value_size, 'x')}};
    test_range_eq(strings, expected);

    co_await check_connection_usable(conn);
}

}  // namespace

int main()
{
    run_coroutine_test(test_success());
    run_coroutine_test(test_success_no_data());
    run_coroutine_test(test_server_error());
    run_coroutine_test(test_request_with_other_commands());
    run_coroutine_test(test_multiplexing());

    return boost::report_errors();
}
