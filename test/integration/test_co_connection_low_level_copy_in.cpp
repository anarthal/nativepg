//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/capy/buffers.hpp>
#include <boost/capy/buffers/make_buffer.hpp>
#include <boost/capy/io_task.hpp>
#include <boost/capy/task.hpp>
#include <boost/core/lightweight_test.hpp>
#include <boost/describe/class.hpp>
#include <boost/describe/operators.hpp>

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

// Extended protocol, nothing fails
capy::task<> test_success_extended_protocol()
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

    // A request that may enter copy-in mode requires exclusive access
    exec_state st;
    if (!BOOST_TEST_EQ(conn.prepare_request(st, req, &handler, exclusivity::exclusive), std::error_code()))
        co_return;
    check_status(st, {.is_prepared = true, .write_phase = write_status::request, .reader_done = false});

    // Writing the request doesn't put us in copy-in mode: only the reader can
    // tell us that the server accepted the COPY and is waiting for data
    if (!check_success(co_await conn.write_request(st)))
        co_return;
    check_status(
        st,
        {.is_prepared = true, .write_phase = write_status::waiting_for_reader, .reader_done = false}
    );

    // Read until the server asks us for data (CopyInResponse)
    while (st.write_phase() == write_status::waiting_for_reader)
    {
        if (!check_success(co_await conn.read_some_response(st)))
            co_return;
    }
    check_status(st, {.is_prepared = true, .write_phase = write_status::copy_data, .reader_done = false});

    // Send the rows
    if (!check_success(co_await write_all_copy_data(conn, st, "1\tone\n2\ttwo\n3\tthree\n"sv)))
        co_return;

    // Terminating the copy hands the connection back to the reader
    if (!check_success(co_await conn.write_copy_done(st)))
        co_return;
    check_status(
        st,
        {.is_prepared = true, .write_phase = write_status::waiting_for_reader, .reader_done = false}
    );

    // Read the rest of the response
    while (!st.read_done())
    {
        if (!check_success(co_await conn.read_some_response(st)))
            co_return;
    }
    check_status(st, {.is_prepared = true, .write_phase = write_status::done, .reader_done = true});
    check_success(st.handler_error());

    // The rows made it into the table
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

    return boost::report_errors();
}
