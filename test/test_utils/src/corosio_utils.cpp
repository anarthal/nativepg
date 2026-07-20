//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/assert/source_location.hpp>
#include <boost/capy/ex/run_async.hpp>
#include <boost/capy/task.hpp>
#include <boost/core/lightweight_test.hpp>
#include <boost/corosio/io_context.hpp>

#include <chrono>
#include <iostream>
#include <system_error>
#include <utility>

#include "test_utils/corosio_utils.hpp"
#include "test_utils/printing.hpp"

void nativepg::test::run_coroutine_test(boost::capy::task<void> test, boost::source_location loc)
{
    // Set a timeout to the tests, so they don't hang on error
    constexpr std::chrono::seconds test_timeout{10};
    bool finished = false;
    auto wrapper_fn = [test = std::move(test), &finished]() mutable -> boost::capy::task<void> {
        co_await std::move(test);
        finished = true;
    };

    // Actually run the test
    boost::corosio::io_context ctx;
    boost::capy::run_async(ctx.get_executor())(wrapper_fn());
    ctx.run_for(test_timeout);

    // Check that it finished
    if (!BOOST_TEST(finished))
        std::cerr << "  Called from " << loc << std::endl;
}

bool nativepg::test::check_success(std::error_code ec, const diagnostics& diag, boost::source_location loc)
{
    bool ok = BOOST_TEST_EQ(ec, std::error_code());
    ok = BOOST_TEST_EQ(diag, diagnostics()) && ok;
    if (!ok)
        std::cerr << "  Called from " << loc << std::endl;
    return ok;
}
