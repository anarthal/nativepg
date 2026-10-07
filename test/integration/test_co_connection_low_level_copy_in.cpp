//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/assert/source_location.hpp>
#include <boost/capy/buffers.hpp>
#include <boost/capy/buffers/make_buffer.hpp>
#include <boost/capy/io_task.hpp>
#include <boost/capy/task.hpp>
#include <boost/core/lightweight_test.hpp>
#include <boost/describe/class.hpp>
#include <boost/describe/operators.hpp>

#include <iostream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "nativepg/co_connection.hpp"
#include "nativepg/exclusivity.hpp"
#include "nativepg/exec_state.hpp"
#include "nativepg/request.hpp"
#include "nativepg/responses/check.hpp"
#include "nativepg/responses/into.hpp"
#include "nativepg/write_status.hpp"
#include "test_utils/co_connection_utils.hpp"
#include "test_utils/corosio_utils.hpp"
#include "test_utils/exec_state_utils.hpp"
#include "test_utils/printing.hpp"
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
    std::vector<row_copy> rows;
    request req_check;
    req_check.add_query("SELECT id, name FROM copy_in_test ORDER BY id");
    if (!co_await checked_exec(conn, req_check, into(rows), loc))
        co_return;
    const row_copy expected[] = {
        {.id = 1, .name = "one"  },
        {.id = 2, .name = "two"  },
        {.id = 3, .name = "three"}
    };
    test_range_eq(rows, expected, loc);

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

    // Both copies landed in the table, and so did the statement between them
    std::vector<row_copy> rows;
    request req_check;
    req_check.add_query("SELECT id, name FROM copy_in_test ORDER BY id");
    if (!co_await checked_exec(conn, req_check, into(rows)))
        co_return;
    const row_copy expected[] = {
        {.id = 1, .name = "one"  },
        {.id = 2, .name = "two"  },
        {.id = 3, .name = "three"}
    };
    test_range_eq(rows, expected);

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
    std::vector<row_copy> rows;
    request req_check;
    req_check.add_query("SELECT id, name FROM copy_in_test ORDER BY id");
    if (!co_await checked_exec(conn, req_check, into(rows)))
        co_return;
    const row_copy expected[] = {
        {.id = 1, .name = "one"  },
        {.id = 2, .name = "two"  },
        {.id = 3, .name = "three"}
    };
    test_range_eq(rows, expected);

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

    return boost::report_errors();
}
