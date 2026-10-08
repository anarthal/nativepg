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
#include <boost/capy/error.hpp>
#include <boost/capy/io_task.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/test/buffer_source.hpp>
#include <boost/core/lightweight_test.hpp>
#include <boost/describe/class.hpp>
#include <boost/describe/operators.hpp>

#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "nativepg/co_connection.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/request.hpp"
#include "nativepg/responses/into.hpp"
#include "test_utils/co_connection_utils.hpp"
#include "test_utils/corosio_utils.hpp"
#include "test_utils/printing.hpp"
#include "test_utils/test_range_eq.hpp"

// Covers co_connection::copy_in

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

}  // namespace

int main()
{
    run_coroutine_test(test_success());

    return boost::report_errors();
}
