//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/assert/source_location.hpp>
#include <boost/capy/ex/run_async.hpp>
#include <boost/capy/ex/this_coro.hpp>
#include <boost/capy/io_result.hpp>
#include <boost/capy/task.hpp>
#include <boost/core/lightweight_test.hpp>
#include <boost/corosio/io_context.hpp>
#include <boost/describe/class.hpp>
#include <boost/describe/operators.hpp>

#include <string>
#include <vector>

#include "nativepg/co_connection.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/request.hpp"
#include "nativepg/response.hpp"
#include "test_utils/ci_server.hpp"
#include "test_utils/corosio_utils.hpp"
#include "test_utils/printing.hpp"

namespace capy = boost::capy;
using namespace nativepg;
using namespace nativepg::test;

namespace {

struct row_int
{
    int value;
};
BOOST_DESCRIBE_STRUCT(row_int, (), (value))

struct row_string
{
    std::string value;
};
BOOST_DESCRIBE_STRUCT(row_string, (), (value))

using boost::describe::operators::operator==;
using boost::describe::operators::operator<<;

// Exec (potentially with pipelining) works
capy::task<> test_exec_success()
{
    // Setup
    diagnostics diag;
    co_connection conn{co_await capy::this_coro::executor};
    if (!check_success(co_await conn.connect(default_connect_params(), &diag), diag))
        co_return;

    // Request and response
    request req;
    req.add_query("SELECT $1 + $2 AS value", {42, 10});
    req.add_query("SELECT $1 AS value", {"abcd"});
    std::vector<row_int> ints;
    std::vector<row_string> strings;

    // Execute
    if (!check_success(co_await conn.exec(req, response{into(ints), into(strings)}, &diag), diag))
        co_return;

    // Check
    std::vector<row_int> ints_expected{{.value = 52}};
    std::vector<row_string> strings_expected{{.value = "abcd"}};
    BOOST_TEST_ALL_EQ(ints.begin(), ints.end(), ints_expected.begin(), ints_expected.end());
    BOOST_TEST_ALL_EQ(strings.begin(), strings.end(), strings_expected.begin(), strings_expected.end());
}

}  // namespace

int main()
{
    run_coroutine_test(test_exec_success());

    return boost::report_errors();
}