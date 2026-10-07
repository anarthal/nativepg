//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/assert/source_location.hpp>
#include <boost/capy/buffers/make_buffer.hpp>
#include <boost/capy/buffers/slice.hpp>
#include <boost/capy/cond.hpp>
#include <boost/capy/error.hpp>
#include <boost/capy/ex/async_event.hpp>
#include <boost/capy/ex/run.hpp>
#include <boost/capy/io_task.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/when_all.hpp>
#include <boost/core/lightweight_test.hpp>
#include <boost/describe/class.hpp>
#include <boost/describe/operators.hpp>

#include <cstddef>
#include <iostream>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "nativepg/client_errc.hpp"
#include "nativepg/co_connection.hpp"
#include "nativepg/exclusivity.hpp"
#include "nativepg/exec_state.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/request.hpp"
#include "nativepg/responses/check.hpp"
#include "nativepg/responses/error_into.hpp"
#include "nativepg/responses/into.hpp"
#include "nativepg/sqlstate_cond.hpp"
#include "nativepg/write_status.hpp"
#include "test_utils/co_connection_utils.hpp"
#include "test_utils/corosio_utils.hpp"
#include "test_utils/exec_state_utils.hpp"
#include "test_utils/printing.hpp"
#include "test_utils/test_cond_eq.hpp"
#include "test_utils/test_range_eq.hpp"

// Covers write_some_copy_data, write_copy_done, write_copy_fail

namespace capy = boost::capy;
using namespace nativepg;
using namespace nativepg::test;
using namespace std::string_view_literals;

namespace {

struct row_copy
{
    int id;
    std::string name;
};
BOOST_DESCRIBE_STRUCT(row_copy, (), (id, name))

using boost::describe::operators::operator==;
using boost::describe::operators::operator<<;

// Sends the entire buffer as copy data, in as many calls as it takes
capy::io_task<> write_all_copy_data(co_connection& conn, exec_state& st, std::string_view buff)
{
    while (!buff.empty())
    {
        auto [ec, bytes] = co_await conn.write_some_copy_data(st, capy::make_buffer(buff));
        if (ec)
            co_return {ec};
        buff = buff.substr(bytes);
    }
    co_return {};
}

// Reads until st.write_phase() is copy_data
capy::io_task<> read_until_copy_data(co_connection& conn, exec_state& st)
{
    while (st.write_phase() == write_status::waiting_for_reader)
    {
        if (auto [ec] = co_await conn.read_some_response(st); ec)
            co_return {ec};
    }

    co_return {};
}

capy::io_task<> read_until_done(co_connection& conn, exec_state& st)
{
    while (!st.read_done())
    {
        if (auto [ec] = co_await conn.read_some_response(st); ec)
            co_return {ec};
    }

    co_return {};
}

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

// Sunny-day case. Might be run with different requests
// (e.g. simple queries/extended protocol)
capy::task<> do_test_success(const request& req_copy_in, boost::source_location loc = BOOST_CURRENT_LOCATION)
{
    // Setup
    auto conn = co_await establish_connection();

    request req_setup;
    req_setup.add_query("CREATE TEMPORARY TABLE copy_in_test (id INT, name TEXT)");
    if (!co_await checked_exec(conn, req_setup, loc))
        co_return;

    check handler;

    // A request that may enter copy-in mode requires exclusive access
    exec_state st;
    if (!BOOST_TEST_EQ(
            conn.prepare_request(st, req_copy_in, &handler, exclusivity::exclusive),
            std::error_code()
        ))
    {
        std::cerr << "  Called from " << loc << std::endl;
        co_return;
    }
    check_status(st, {.is_prepared = true, .write_phase = write_status::request, .reader_done = false}, loc);

    // Writing the request doesn't put us in copy-in mode: only the reader can
    // tell us that the server accepted the COPY and is waiting for data
    if (!check_success(co_await conn.write_request(st), loc))
        co_return;
    check_status(
        st,
        {.is_prepared = true, .write_phase = write_status::waiting_for_reader, .reader_done = false},
        loc
    );

    // Read until the server asks us for data (CopyInResponse)
    if (!check_success(co_await read_until_copy_data(conn, st)))
        co_return;
    check_status(
        st,
        {.is_prepared = true, .write_phase = write_status::copy_data, .reader_done = false},
        loc
    );

    // Send the rows
    if (!check_success(co_await write_all_copy_data(conn, st, "1\tone\n2\ttwo\n3\tthree\n"sv), loc))
        co_return;

    // Terminating the copy hands the connection back to the reader
    if (!check_success(co_await conn.write_copy_done(st), loc))
        co_return;
    check_status(
        st,
        {.is_prepared = true, .write_phase = write_status::waiting_for_reader, .reader_done = false},
        loc
    );

    // Read the rest of the response
    if (!check_success(co_await read_until_done(conn, st)))
        co_return;
    check_status(st, {.is_prepared = true, .write_phase = write_status::done, .reader_done = true}, loc);
    check_success(st.handler_error(), loc);

    // The rows made it into the table
    const row_copy expected[] = {
        {.id = 1, .name = "one"  },
        {.id = 2, .name = "two"  },
        {.id = 3, .name = "three"},
    };
    co_await check_copy_rows(conn, expected);

    co_await check_connection_usable(conn, loc);
}

capy::task<> test_success_extended_protocol()
{
    request req;
    req.add_query("COPY copy_in_test FROM STDIN");
    co_await do_test_success(req);
}

capy::task<> test_success_simple_query_protocol()
{
    request req;
    req.add_simple_query("COPY copy_in_test FROM STDIN");
    co_await do_test_success(req);
}

// Extra Sync messages are tolerated
capy::task<> test_success_extended_protocol_extra_syncs()
{
    request req;
    req.add_query("COPY copy_in_test FROM STDIN");
    req.add_sync();
    req.add_sync();
    co_await do_test_success(req);
}

capy::task<> test_success_simple_query_protocol_extra_syncs()
{
    request req;
    req.add_simple_query("COPY copy_in_test FROM STDIN");
    req.add_sync();
    req.add_sync();
    req.add_sync();
    co_await do_test_success(req);
}

// A single simple query may contain several COPY ... FROM STDIN statements,
// with other statements in between. Each one takes the writer back to copy_data
capy::task<> test_success_simple_query_several_copies()
{
    // Setup
    auto conn = co_await establish_connection();

    request req_setup;
    req_setup.add_query("CREATE TEMPORARY TABLE copy_in_test (id INT, name TEXT)");
    if (!co_await checked_exec(conn, req_setup))
        co_return;

    // The statement between the two copies makes the server go back to its
    // regular flow before asking us for data again.
    // This works even in the presence of trailing syncs.
    request req;
    req.add_simple_query(
        "COPY copy_in_test FROM STDIN;"
        "INSERT INTO copy_in_test VALUES (2, 'two');"
        "COPY copy_in_test FROM STDIN"
    );
    req.add_sync();
    req.add_sync();
    check handler;

    exec_state st;
    if (!BOOST_TEST_EQ(conn.prepare_request(st, req, &handler, exclusivity::exclusive), std::error_code()))
        co_return;
    if (!check_success(co_await conn.write_request(st)))
        co_return;

    // First copy
    if (!check_success(co_await read_until_copy_data(conn, st)))
        co_return;
    check_status(st, {.is_prepared = true, .write_phase = write_status::copy_data, .reader_done = false});

    if (!check_success(co_await write_all_copy_data(conn, st, "1\tone\n"sv)))
        co_return;
    if (!check_success(co_await conn.write_copy_done(st)))
        co_return;
    check_status(
        st,
        {.is_prepared = true, .write_phase = write_status::waiting_for_reader, .reader_done = false}
    );

    // Second copy
    if (!check_success(co_await read_until_copy_data(conn, st)))
        co_return;
    check_status(st, {.is_prepared = true, .write_phase = write_status::copy_data, .reader_done = false});

    if (!check_success(co_await write_all_copy_data(conn, st, "3\tthree\n"sv)))
        co_return;
    if (!check_success(co_await conn.write_copy_done(st)))
        co_return;
    check_status(
        st,
        {.is_prepared = true, .write_phase = write_status::waiting_for_reader, .reader_done = false}
    );

    // Read the rest of the response
    if (!check_success(co_await read_until_done(conn, st)))
        co_return;
    check_status(st, {.is_prepared = true, .write_phase = write_status::done, .reader_done = true});
    check_success(st.handler_error());

    // Both copies and the insert had effect
    const row_copy expected[] = {
        {.id = 1, .name = "one"  },
        {.id = 2, .name = "two"  },
        {.id = 3, .name = "three"}
    };
    co_await check_copy_rows(conn, expected);

    co_await check_connection_usable(conn);
}

// Copy data can be handed over in as many calls as the caller likes
capy::task<> test_success_several_writes()
{
    // Setup
    auto conn = co_await establish_connection();

    request req_setup;
    req_setup.add_query("CREATE TEMPORARY TABLE copy_in_test (id INT, name TEXT)");
    if (!co_await checked_exec(conn, req_setup))
        co_return;

    request req;
    req.add_query("COPY copy_in_test FROM STDIN");
    check handler;

    exec_state st;
    if (!BOOST_TEST_EQ(conn.prepare_request(st, req, &handler, exclusivity::exclusive), std::error_code()))
        co_return;
    if (!check_success(co_await conn.write_request(st)))
        co_return;

    // Wait until the server asks us for data
    if (!check_success(co_await read_until_copy_data(conn, st)))
        co_return;
    check_status(st, {.is_prepared = true, .write_phase = write_status::copy_data, .reader_done = false});

    // Split points are arbitrary
    if (!check_success(co_await write_all_copy_data(conn, st, "1")) ||
        !check_success(co_await write_all_copy_data(conn, st, "\tone\n2\tt")) ||
        !check_success(co_await write_all_copy_data(conn, st, "wo\n3\tthr")) ||
        !check_success(co_await write_all_copy_data(conn, st, "ee\n")))
        co_return;
    check_status(st, {.is_prepared = true, .write_phase = write_status::copy_data, .reader_done = false});

    if (!check_success(co_await conn.write_copy_done(st)))
        co_return;

    // Read the rest of the response
    if (!check_success(co_await read_until_done(conn, st)))
        co_return;
    check_status(st, {.is_prepared = true, .write_phase = write_status::done, .reader_done = true});
    check_success(st.handler_error());

    // The server reassembled the stream regardless of how we split it
    const row_copy expected[] = {
        {.id = 1, .name = "one"  },
        {.id = 2, .name = "two"  },
        {.id = 3, .name = "three"}
    };
    co_await check_copy_rows(conn, expected);

    co_await check_connection_usable(conn);
}

// Copy data may be supplied as any ConstBufferSequence
capy::task<> test_success_buffer_sequence()
{
    // Setup
    auto conn = co_await establish_connection();

    request req_setup;
    req_setup.add_query("CREATE TEMPORARY TABLE copy_in_test (id INT, name TEXT)");
    if (!co_await checked_exec(conn, req_setup))
        co_return;

    request req;
    req.add_query("COPY copy_in_test FROM STDIN");
    check handler;

    exec_state st;
    if (!BOOST_TEST_EQ(conn.prepare_request(st, req, &handler, exclusivity::exclusive), std::error_code()))
        co_return;
    if (!check_success(co_await conn.write_request(st)))
        co_return;

    // Wait until the server asks us for data
    if (!check_success(co_await read_until_copy_data(conn, st)))
        co_return;
    check_status(st, {.is_prepared = true, .write_phase = write_status::copy_data, .reader_done = false});

    // A sequence with several buffers
    constexpr auto part0 = "1\tone\n2\tt"sv;
    constexpr auto part1 = "wo\n3\tthr"sv;
    constexpr auto part2 = "ee\n"sv;
    const std::array<capy::const_buffer, 4u> seq{
        capy::make_buffer(part0),
        capy::make_buffer(part1),
        capy::const_buffer(),
        capy::make_buffer(part2),
    };
    const auto total_size = part0.size() + part1.size() + part2.size();

    // Transfer all data
    for (std::size_t transferred = 0u; transferred < total_size;)
    {
        auto [ec, bytes] = co_await conn.write_some_copy_data(st, boost::capy::sans_prefix(seq, transferred));
        if (!BOOST_TEST_EQ(ec, std::error_code()))
            co_return;
        transferred += bytes;
    }
    check_status(st, {.is_prepared = true, .write_phase = write_status::copy_data, .reader_done = false});

    if (!check_success(co_await conn.write_copy_done(st)))
        co_return;

    // Read the rest of the response
    if (!check_success(co_await read_until_done(conn, st)))
        co_return;
    check_status(st, {.is_prepared = true, .write_phase = write_status::done, .reader_done = true});
    check_success(st.handler_error());

    // The server saw a single, contiguous stream
    const row_copy expected[] = {
        {.id = 1, .name = "one"  },
        {.id = 2, .name = "two"  },
        {.id = 3, .name = "three"}
    };
    co_await check_copy_rows(conn, expected);

    co_await check_connection_usable(conn);
}

// Calling write_copy_done without transferring data first is OK
capy::task<> test_success_no_data()
{
    // Setup
    auto conn = co_await establish_connection();

    request req_setup;
    req_setup.add_query("CREATE TEMPORARY TABLE copy_in_test (id INT, name TEXT)");
    if (!co_await checked_exec(conn, req_setup))
        co_return;

    request req;
    req.add_query("COPY copy_in_test FROM STDIN");
    check handler;

    exec_state st;
    if (!BOOST_TEST_EQ(conn.prepare_request(st, req, &handler, exclusivity::exclusive), std::error_code()))
        co_return;
    if (!check_success(co_await conn.write_request(st)))
        co_return;

    // Wait until the server asks us for data
    if (!check_success(co_await read_until_copy_data(conn, st)))
        co_return;
    check_status(st, {.is_prepared = true, .write_phase = write_status::copy_data, .reader_done = false});

    // Terminate the copy without ever calling write_some_copy_data
    if (!check_success(co_await conn.write_copy_done(st)))
        co_return;
    check_status(
        st,
        {.is_prepared = true, .write_phase = write_status::waiting_for_reader, .reader_done = false}
    );

    // Read the rest of the response
    if (!check_success(co_await read_until_done(conn, st)))
        co_return;
    check_status(st, {.is_prepared = true, .write_phase = write_status::done, .reader_done = true});
    check_success(st.handler_error());

    // The copy completed without copying anything
    co_await check_copy_rows(conn, {});

    co_await check_connection_usable(conn);
}

// Reader and writer can run in parallel
capy::task<> test_success_parallel()
{
    // Setup
    auto conn = co_await establish_connection();

    request req_setup;
    req_setup.add_query("CREATE TEMPORARY TABLE copy_in_test (id INT, name TEXT)");
    if (!co_await checked_exec(conn, req_setup))
        co_return;

    request req;
    req.add_query("COPY copy_in_test FROM STDIN");
    check handler;

    exec_state st;
    if (!BOOST_TEST_EQ(conn.prepare_request(st, req, &handler, exclusivity::exclusive), std::error_code()))
        co_return;

    capy::async_event copy_in_received;

    auto [ec, writer_dummy, reader_dummy] = co_await capy::when_all(
        [&]() -> capy::io_task<> {
            // Writer. Write the request
            if (!check_success(co_await conn.write_request(st)))
                co_return {capy::error::canceled};

            // Wait until the reader tells us that the server wants data
            if (!check_success(co_await copy_in_received.wait()))
                co_return {capy::error::canceled};

            // Write all the data
            if (!check_success(co_await write_all_copy_data(conn, st, "1\tone\n2\ttwo\n3\tthree\n"sv)))
                co_return {capy::error::canceled};

            // Finish
            if (!check_success(co_await conn.write_copy_done(st)))
                co_return {capy::error::canceled};

            co_return {};
        }(),

        [&]() -> capy::io_task<> {
            // Reader. Needs to notify the writer to start writing copy data
            while (!st.read_done())
            {
                if (!check_success(co_await conn.read_some_response(st)))
                    co_return {capy::error::canceled};

                if (st.write_phase() == write_status::copy_data)
                    copy_in_received.set();
            }

            // If we received no request for copy data, unblock the writer
            co_return {copy_in_received.is_set() ? std::error_code{} : capy::error::canceled};
        }()
    );

    if (ec)
        co_return;
    check_status(st, {.is_prepared = true, .write_phase = write_status::done, .reader_done = true});
    check_success(st.handler_error());

    // The rows were transferred
    const row_copy expected[] = {
        {.id = 1, .name = "one"  },
        {.id = 2, .name = "two"  },
        {.id = 3, .name = "three"}
    };
    co_await check_copy_rows(conn, expected);

    co_await check_connection_usable(conn);
}

// write_copy_fail works
capy::task<> test_copy_fail()
{
    // Setup
    auto conn = co_await establish_connection();

    request req_setup;
    req_setup.add_query("CREATE TEMPORARY TABLE copy_in_test (id INT, name TEXT)");
    if (!co_await checked_exec(conn, req_setup))
        co_return;

    request req;
    req.add_query("COPY copy_in_test FROM STDIN");
    check handler;

    exec_state st;
    if (!BOOST_TEST_EQ(conn.prepare_request(st, req, &handler, exclusivity::exclusive), std::error_code()))
        co_return;
    if (!check_success(co_await conn.write_request(st)))
        co_return;

    // Wait until the server asks us for data, and send some
    if (!check_success(co_await read_until_copy_data(conn, st)))
        co_return;
    check_status(st, {.is_prepared = true, .write_phase = write_status::copy_data, .reader_done = false});

    if (!check_success(co_await write_all_copy_data(conn, st, "1\tone\n2\ttwo\n"sv)))
        co_return;

    // Change our mind. Like write_copy_done, this hands the connection
    // back to the reader
    constexpr auto fail_msg = "test_copy_fail_marker"sv;
    if (!check_success(co_await conn.write_copy_fail(st, fail_msg)))
        co_return;
    check_status(
        st,
        {.is_prepared = true, .write_phase = write_status::waiting_for_reader, .reader_done = false}
    );

    // Read the rest of the response
    if (!check_success(co_await read_until_done(conn, st)))
        co_return;
    check_status(st, {.is_prepared = true, .write_phase = write_status::done, .reader_done = true});

    // The server reports an error containing the message that we sent
    test_cond_eq(st.handler_error().code, sqlstate_cond::query_canceled);
    auto pred = [](std::string_view big, std::string_view small) {
        return big.find(small) != std::string_view::npos;
    };
    BOOST_TEST_WITH(st.handler_error().diag.message(), fail_msg, pred);

    // The rows we sent before failing were discarded
    co_await check_copy_rows(conn, {});

    // Failing a copy is not a protocol error
    co_await check_connection_usable(conn);
}

// write_copy_fail might be called without transferring any data first
capy::task<> test_copy_fail_no_data()
{
    // Setup
    auto conn = co_await establish_connection();

    request req_setup;
    req_setup.add_query("CREATE TEMPORARY TABLE copy_in_test (id INT, name TEXT)");
    if (!co_await checked_exec(conn, req_setup))
        co_return;

    request req;
    req.add_query("COPY copy_in_test FROM STDIN");
    check handler;

    exec_state st;
    if (!BOOST_TEST_EQ(conn.prepare_request(st, req, &handler, exclusivity::exclusive), std::error_code()))
        co_return;
    if (!check_success(co_await conn.write_request(st)))
        co_return;

    // Wait until the server asks us for data
    if (!check_success(co_await read_until_copy_data(conn, st)))
        co_return;
    check_status(st, {.is_prepared = true, .write_phase = write_status::copy_data, .reader_done = false});

    // Abort the transfer
    constexpr auto fail_msg = "test_copy_fail_marker"sv;
    if (!check_success(co_await conn.write_copy_fail(st, fail_msg)))
        co_return;
    check_status(
        st,
        {.is_prepared = true, .write_phase = write_status::waiting_for_reader, .reader_done = false}
    );

    // Read the rest of the response
    if (!check_success(co_await read_until_done(conn, st)))
        co_return;
    check_status(st, {.is_prepared = true, .write_phase = write_status::done, .reader_done = true});

    // The server reports an error containing the message that we sent
    test_cond_eq(st.handler_error().code, sqlstate_cond::query_canceled);
    auto pred = [](std::string_view big, std::string_view small) {
        return big.find(small) != std::string_view::npos;
    };
    BOOST_TEST_WITH(st.handler_error().diag.message(), fail_msg, pred);

    // No data was copied
    co_await check_copy_rows(conn, {});

    // Failing a copy is not a protocol error
    co_await check_connection_usable(conn);
}

// The server rejects the data while we are sending it
capy::task<> test_server_error_mid_transfer()
{
    // Setup
    auto conn = co_await establish_connection();

    request req_setup;
    req_setup.add_query("CREATE TEMPORARY TABLE copy_in_test (id INT, name TEXT)");
    if (!co_await checked_exec(conn, req_setup))
        co_return;

    request req;
    req.add_query("COPY copy_in_test FROM STDIN");
    extended_error err;
    error_into handler(check(), err);

    exec_state st;
    if (!BOOST_TEST_EQ(conn.prepare_request(st, req, &handler, exclusivity::exclusive), std::error_code()))
        co_return;
    if (!check_success(co_await conn.write_request(st)))
        co_return;

    if (!check_success(co_await read_until_copy_data(conn, st)))
        co_return;
    check_status(st, {.is_prepared = true, .write_phase = write_status::copy_data, .reader_done = false});

    // The first row is fine, the second one has an extra column for a table
    // with two of them. The server rejects it as it parses the stream
    if (!check_success(co_await write_all_copy_data(conn, st, "1\tone\n2\ttwo\textra\n"sv)))
        co_return;

    // Writing is still allowed: the server's error hasn't been read yet, so as
    // far as the writer is concerned the copy is still running
    check_status(st, {.is_prepared = true, .write_phase = write_status::copy_data, .reader_done = false});
    if (!check_success(co_await write_all_copy_data(conn, st, "3\tthree\n"sv)))
        co_return;
    check_status(st, {.is_prepared = true, .write_phase = write_status::copy_data, .reader_done = false});

    // Read the error message
    while (!err.code)
    {
        if (!check_success(co_await conn.read_some_response(st)))
            co_return;
    }
    test_cond_eq(err.code, sqlstate_cond::bad_copy_file_format);

    // We're still stuck in Copy-in mode at this point.
    // We still need to send the Sync messages to restore normality
    if (!check_success(co_await conn.write_copy_done(st)))
        co_return;
    check_status(
        st,
        {.is_prepared = true, .write_phase = write_status::waiting_for_reader, .reader_done = false}
    );

    // Read the rest of the response
    if (!check_success(co_await read_until_done(conn, st)))
        co_return;
    check_status(st, {.is_prepared = true, .write_phase = write_status::done, .reader_done = true});

    // The failure is reported by the handler
    test_cond_eq(st.handler_error().code, sqlstate_cond::bad_copy_file_format);
    BOOST_TEST_NOT(st.handler_error().diag.message().empty());

    // No data was copied
    co_await check_copy_rows(conn, {});

    // A rejected copy is not a protocol error
    co_await check_connection_usable(conn);
}

// write_some_copy_data can be cancelled and retried
capy::task<> test_cancel_retry_copy_data()
{
    // Setup
    auto conn = co_await establish_connection();

    request req_setup;
    req_setup.add_query("CREATE TEMPORARY TABLE copy_in_test (id INT, name TEXT)");
    if (!co_await checked_exec(conn, req_setup))
        co_return;

    request req;
    req.add_query("COPY copy_in_test FROM STDIN");
    check handler;

    exec_state st;
    if (!BOOST_TEST_EQ(conn.prepare_request(st, req, &handler, exclusivity::exclusive), std::error_code()))
        co_return;
    if (!check_success(co_await conn.write_request(st)))
        co_return;

    if (!check_success(co_await read_until_copy_data(conn, st)))
        co_return;
    check_status(st, {.is_prepared = true, .write_phase = write_status::copy_data, .reader_done = false});

    // Run the write under a token that is already stopped, so it reports cancellation
    constexpr auto copy_data = "1\tone\n2\ttwo\n3\tthree\n"sv;
    std::stop_source stop_src;
    stop_src.request_stop();
    auto [write_ec, bytes] = co_await capy::run(stop_src.get_token())(
        conn.write_some_copy_data(st, capy::make_buffer(copy_data))
    );
    test_cond_eq(write_ec, capy::cond::canceled);
    BOOST_TEST_LE(bytes, copy_data.size());

    check_status(st, {.is_prepared = true, .write_phase = write_status::copy_data, .reader_done = false});

    // We can retry with the bytes that weren't transferred
    if (!check_success(co_await write_all_copy_data(conn, st, copy_data.substr(bytes))))
        co_return;
    if (!check_success(co_await conn.write_copy_done(st)))
        co_return;

    // Read the rest of the response
    if (!check_success(co_await read_until_done(conn, st)))
        co_return;
    check_status(st, {.is_prepared = true, .write_phase = write_status::done, .reader_done = true});
    check_success(st.handler_error());

    // The server got the right data
    const row_copy expected[] = {
        {.id = 1, .name = "one"  },
        {.id = 2, .name = "two"  },
        {.id = 3, .name = "three"}
    };
    co_await check_copy_rows(conn, expected);

    co_await check_connection_usable(conn);
}

// TODO: cancelling write_copy_done/write_copy_fail is fatal

// A COPY ... FROM STDIN must be the last thing in its request (fatal error)
capy::task<> do_test_copy_in_not_last(
    const request& req_copy_in,
    boost::source_location loc = BOOST_CURRENT_LOCATION
)
{
    // Setup
    auto conn = co_await establish_connection();

    request req_setup;
    req_setup.add_query("CREATE TEMPORARY TABLE copy_in_test (id INT, name TEXT)");
    if (!co_await checked_exec(conn, req_setup, loc))
        co_return;

    check handler;

    // Preparing and writing the request both succeed
    exec_state st;
    if (!BOOST_TEST_EQ(
            conn.prepare_request(st, req_copy_in, &handler, exclusivity::exclusive),
            std::error_code()
        ))
    {
        std::cerr << "  Called from " << loc << std::endl;
        co_return;
    }
    if (!check_success(co_await conn.write_request(st), loc))
        co_return;

    // The reader finds out when the CopyInResponse arrives
    auto [ec] = co_await read_until_copy_data(conn, st);
    if (!BOOST_TEST_EQ(ec, make_error_code(client_errc::copy_in_not_last)))
        std::cerr << "  Called from " << loc << std::endl;

    // The copy never started, and the operation didn't complete.
    // The connection is unusable
    check_status(
        st,
        {.is_prepared = true, .write_phase = write_status::waiting_for_reader, .reader_done = false},
        loc
    );
}

capy::task<> test_copy_in_not_last_extended_protocol()
{
    request req;
    req.add_query("COPY copy_in_test FROM STDIN");
    req.add_query("SELECT 1");
    co_await do_test_copy_in_not_last(req);
}

capy::task<> test_copy_in_not_last_simple_query_protocol()
{
    request req;
    req.add_simple_query("COPY copy_in_test FROM STDIN");
    req.add_simple_query("SELECT 1");
    co_await do_test_copy_in_not_last(req);
}

// Copy-in requires exclusive mode
capy::task<> test_copy_in_not_exclusive()
{
    // Setup
    auto conn = co_await establish_connection();

    request req_setup;
    req_setup.add_query("CREATE TEMPORARY TABLE copy_in_test (id INT, name TEXT)");
    if (!co_await checked_exec(conn, req_setup))
        co_return;

    check handler;

    // Preparing and writing the request both succeed
    request req;
    req.add_query("COPY copy_in_test FROM STDIN");
    exec_state st;
    if (!BOOST_TEST_EQ(conn.prepare_request(st, req, &handler, exclusivity::shared), std::error_code()))
        co_return;
    if (!check_success(co_await conn.write_request(st)))
        co_return;

    // The reader receives CopyInResponse and fails
    std::error_code ec;
    while (!ec && !st.read_done())
        ec = (co_await conn.read_some_response(st)).ec;
    BOOST_TEST_EQ(ec, std::error_code(client_errc::requires_exclusive));

    // The connection is not usable
}

//
// State checks
//

// A state that was never prepared has nothing to write to
capy::task<> test_not_prepared()
{
    // Setup
    auto conn = co_await establish_connection();
    exec_state st;

    auto [ec1, bytes] = co_await conn.write_some_copy_data(st, capy::make_buffer("abc"sv));
    BOOST_TEST_EQ(ec1, std::error_code(client_errc::invalid_state));

    auto [ec2] = co_await conn.write_copy_done(st);
    BOOST_TEST_EQ(ec2, std::error_code(client_errc::invalid_state));

    auto [ec3] = co_await conn.write_copy_fail(st, "message");
    BOOST_TEST_EQ(ec3, std::error_code(client_errc::invalid_state));

    // Nothing was written, so the connection is untouched
    co_await check_connection_usable(conn);
}

// The request has to reach the server before the server can ask for data
capy::task<> test_request_not_written()
{
    auto conn = co_await establish_connection();

    request req;
    req.add_query("COPY copy_in_test FROM STDIN");
    check handler;

    exec_state st;
    if (!BOOST_TEST_EQ(conn.prepare_request(st, req, &handler, exclusivity::exclusive), std::error_code()))
        co_return;

    auto [ec1, bytes] = co_await conn.write_some_copy_data(st, capy::make_buffer("abc"sv));
    BOOST_TEST_EQ(ec1, std::error_code(client_errc::invalid_state));

    auto [ec2] = co_await conn.write_copy_done(st);
    BOOST_TEST_EQ(ec2, std::error_code(client_errc::invalid_state));

    auto [ec3] = co_await conn.write_copy_fail(st, "message");
    BOOST_TEST_EQ(ec3, std::error_code(client_errc::invalid_state));

    // Nothing was written, so the connection is untouched
    co_await check_connection_usable(conn);
}

// The request was written, but the server hasn't asked for data
capy::task<> test_not_in_copy_data()
{
    auto conn = co_await establish_connection();

    request req;
    req.add_query("SELECT 1");
    check handler;

    exec_state st;
    if (!BOOST_TEST_EQ(conn.prepare_request(st, req, &handler, exclusivity::exclusive), std::error_code()))
        co_return;
    if (!check_success(co_await conn.write_request(st)))
        co_return;
    check_status(
        st,
        {.is_prepared = true, .write_phase = write_status::waiting_for_reader, .reader_done = false}
    );

    auto [ec1, bytes] = co_await conn.write_some_copy_data(st, capy::make_buffer("abc"sv));
    BOOST_TEST_EQ(ec1, std::error_code(client_errc::invalid_state));

    auto [ec2] = co_await conn.write_copy_done(st);
    BOOST_TEST_EQ(ec2, std::error_code(client_errc::invalid_state));

    auto [ec3] = co_await conn.write_copy_fail(st, "message");
    BOOST_TEST_EQ(ec3, std::error_code(client_errc::invalid_state));

    // The rejected call didn't disturb the response
    if (!check_success(co_await read_until_done(conn, st)))
        co_return;
    check_success(st.handler_error());
    co_await check_connection_usable(conn);
}

// Once the operation is over, there is nothing left to write
capy::task<> test_already_done()
{
    // Setup
    auto conn = co_await establish_connection();

    request req;
    req.add_query("SELECT 1");
    check handler;

    exec_state st;

    // Run the operation until it's done
    if (!BOOST_TEST_EQ(conn.prepare_request(st, req, &handler, exclusivity::exclusive), std::error_code()))
        co_return;
    if (!check_success(co_await conn.write_request(st)))
        co_return;
    if (!check_success(co_await read_until_done(conn, st)))
        co_return;
    check_status(st, {.is_prepared = true, .write_phase = write_status::done, .reader_done = true});

    // Try to write
    auto [ec1, bytes] = co_await conn.write_some_copy_data(st, capy::make_buffer("abc"sv));
    BOOST_TEST_EQ(ec1, std::error_code(client_errc::invalid_state));

    auto [ec2] = co_await conn.write_copy_done(st);
    BOOST_TEST_EQ(ec2, std::error_code(client_errc::invalid_state));

    auto [ec3] = co_await conn.write_copy_fail(st, "message");
    BOOST_TEST_EQ(ec3, std::error_code(client_errc::invalid_state));

    // We didn't disturb the connection
    co_await check_connection_usable(conn);
}

// The writer functions are mutually exclusive
capy::task<> test_already_running()
{
    // Setup
    auto conn = co_await establish_connection();

    request req_setup;
    req_setup.add_query("CREATE TEMPORARY TABLE copy_in_test (id INT, name TEXT)");
    if (!co_await checked_exec(conn, req_setup))
        co_return;

    request req;
    req.add_query("COPY copy_in_test FROM STDIN");
    check handler;

    // Run until CopyInResponse
    exec_state st;
    if (!BOOST_TEST_EQ(conn.prepare_request(st, req, &handler, exclusivity::exclusive), std::error_code()))
        co_return;
    if (!check_success(co_await conn.write_request(st)))
        co_return;
    if (!check_success(co_await read_until_copy_data(conn, st)))
        co_return;

    static_cast<void>(co_await capy::when_all(
        [&]() -> capy::io_task<> {
            // Takes the write side
            auto [ec, bytes] = co_await conn.write_some_copy_data(st, capy::make_buffer("1\tone\n"sv));
            check_success(ec);
            co_return {};
        }(),

        [&]() -> capy::io_task<> {
            // Launched while the first one is still in flight
            auto [ec, bytes] = co_await conn.write_some_copy_data(st, capy::make_buffer("abc"sv));
            BOOST_TEST_EQ(ec, std::error_code(client_errc::already_running));
            co_return {};
        }(),

        [&]() -> capy::io_task<> {
            // Launched while the first one is still in flight
            auto [ec] = co_await conn.write_copy_done(st);
            BOOST_TEST_EQ(ec, std::error_code(client_errc::already_running));
            co_return {};
        }(),

        [&]() -> capy::io_task<> {
            // Launched while the first one is still in flight
            auto [ec] = co_await conn.write_copy_fail(st, "message");
            BOOST_TEST_EQ(ec, std::error_code(client_errc::already_running));
            co_return {};
        }()
    ));

    // The rejected call didn't disturb the copy
    if (!check_success(co_await conn.write_copy_done(st)))
        co_return;
    if (!check_success(co_await read_until_done(conn, st)))
        co_return;
    check_success(st.handler_error());

    co_await check_connection_usable(conn);
}

}  // namespace

int main()
{
    run_coroutine_test(test_success_extended_protocol());
    run_coroutine_test(test_success_simple_query_protocol());
    run_coroutine_test(test_success_extended_protocol_extra_syncs());
    run_coroutine_test(test_success_simple_query_protocol_extra_syncs());
    run_coroutine_test(test_success_simple_query_several_copies());
    run_coroutine_test(test_success_several_writes());
    run_coroutine_test(test_success_buffer_sequence());
    run_coroutine_test(test_success_no_data());
    run_coroutine_test(test_success_parallel());

    run_coroutine_test(test_copy_fail());
    run_coroutine_test(test_copy_fail_no_data());
    run_coroutine_test(test_server_error_mid_transfer());

    run_coroutine_test(test_cancel_retry_copy_data());

    run_coroutine_test(test_copy_in_not_last_extended_protocol());
    run_coroutine_test(test_copy_in_not_last_simple_query_protocol());
    run_coroutine_test(test_copy_in_not_exclusive());

    run_coroutine_test(test_not_prepared());
    run_coroutine_test(test_request_not_written());
    run_coroutine_test(test_not_in_copy_data());
    run_coroutine_test(test_already_done());
    run_coroutine_test(test_already_running());

    return boost::report_errors();
}
