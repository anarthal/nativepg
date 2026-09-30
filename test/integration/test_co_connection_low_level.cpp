//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/capy/io_task.hpp>
#include <boost/capy/task.hpp>
#include <boost/core/lightweight_test.hpp>
#include <boost/describe/class.hpp>
#include <boost/describe/operators.hpp>

#include <cstdint>
#include <system_error>
#include <vector>

#include "nativepg/co_connection.hpp"
#include "nativepg/exec_state.hpp"
#include "nativepg/request.hpp"
#include "nativepg/responses/check.hpp"
#include "nativepg/responses/into.hpp"
#include "nativepg/responses/response.hpp"
#include "test_utils/co_connection_utils.hpp"
#include "test_utils/corosio_utils.hpp"
#include "test_utils/test_range_eq.hpp"

namespace capy = boost::capy;
using namespace nativepg;
using namespace nativepg::test;

namespace {

struct row_int
{
    int value;
};
BOOST_DESCRIBE_STRUCT(row_int, (), (value))

using boost::describe::operators::operator==;
using boost::describe::operators::operator<<;

// Sunny-day case. This executes write fully before reading, as opposed to exec()
capy::task<> test_success()
{
    // Setup
    auto conn = co_await establish_connection();
    request req;
    req.add_query("SELECT $1 + $2 AS value", 42, 10);
    req.add_query("SELECT 90 AS value");
    std::vector<row_int> rows1, rows2;
    response handler{into(rows1), into(rows2)};

    // A fresh state holds nothing
    exec_state st;
    BOOST_TEST(!st.is_prepared());
    BOOST_TEST(!st.write_done());
    BOOST_TEST(!st.read_done());

    // Prepare
    if (!BOOST_TEST_EQ(conn.prepare_request(st, req, &handler), std::error_code()))
        co_return;
    BOOST_TEST(st.is_prepared());
    BOOST_TEST(!st.write_done());
    BOOST_TEST(!st.read_done());

    // Write the request
    if (!check_success(co_await conn.write_request(st)))
        co_return;
    BOOST_TEST(st.is_prepared());
    BOOST_TEST(st.write_done());
    BOOST_TEST(!st.read_done());

    // Read the response. A single call only guarantees progress, not completion
    while (!st.read_done())
    {
        if (!check_success(co_await conn.read_some_response(st)))
            co_return;
    }

    // Check what the handler produced
    check_success(st.handler_error());
    test_range_eq(rows1, std::vector<row_int>{{.value = 52}});
    test_range_eq(rows2, std::vector<row_int>{{.value = 90}});

    // Having read the response in full, we left nothing behind
    co_await check_connection_usable(conn);
}

// Instead of blocking, read_some_response returns when only some messages are read
// in one read_some
capy::task<> test_partial_read()
{
    // Setup
    auto conn = co_await establish_connection(), locker = co_await establish_connection();

    // Take the lock, so the second query in our request can't complete.
    // Note: different tests should use different IDs
    constexpr std::int64_t lock_id = 21;
    if (!co_await checked_exec(locker, request().add_query("SELECT pg_advisory_lock($1)", lock_id)))
        co_return;

    // The first query answers right away, the second one blocks on the lock
    request req;
    req.add_query("SELECT $1 AS value", 42);
    req.add_query("SELECT pg_advisory_lock($1)", lock_id);
    std::vector<row_int> rows;
    response handler{into(rows), check_execute()};

    // Prepare and write
    exec_state st;
    if (!BOOST_TEST_EQ(conn.prepare_request(st, req, &handler), std::error_code()))
        co_return;
    if (!check_success(co_await conn.write_request(st)))
        co_return;

    // Read until the first resultset arrives. The server still owes us the second
    // one at this point, so read_some_response had to return without completing
    while (rows.empty())
    {
        if (!check_success(co_await conn.read_some_response(st)))
            co_return;
    }
    BOOST_TEST(!st.read_done());
    test_range_eq(rows, std::vector<row_int>{{.value = 42}});

    // Unblock the rest of the response
    if (!check_success(co_await locker.shutdown()))
        co_return;

    // Read the remainder
    while (!st.read_done())
    {
        if (!check_success(co_await conn.read_some_response(st)))
            co_return;
    }

    // Check
    check_success(st.handler_error());

    co_await check_connection_usable(conn);
}

// // A state that has been driven to completion can be reused for another request
// capy::task<> test_reuse_state()
// {
//     // Setup
//     auto conn = co_await establish_connection();

//     exec_state st;

//     for (int expected : {1, 2})
//     {
//         request req;
//         req.add_query("SELECT $1 AS value", expected);
//         std::vector<row_int> rows;
//         auto handler = into(rows);

//         // prepare_request cleans up whatever the previous round left
//         if (!BOOST_TEST_EQ(conn.prepare_request(st, req, &handler), std::error_code()))
//             co_return;
//         if (!check_success(co_await conn.write_request(st)))
//             co_return;
//         while (!st.read_done())
//         {
//             if (!check_success(co_await conn.read_some_response(st)))
//                 co_return;
//         }

//         check_success(st.handler_error());
//         test_range_eq(rows, std::vector<row_int>{{.value = expected}});

//         // The request and the handler die at the end of this iteration, so the
//         // state must not outlive them holding references
//         st.reset();
//     }

//     co_await check_connection_usable(conn);
// }

}  // namespace

int main()
{
    run_coroutine_test(test_success());
    run_coroutine_test(test_partial_read());

    return boost::report_errors();
}
