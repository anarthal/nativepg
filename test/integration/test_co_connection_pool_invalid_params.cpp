//
// Copyright (c) 2019-2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/assert/source_location.hpp>
#include <boost/capy/task.hpp>
#include <boost/core/lightweight_test.hpp>
#include <boost/corosio/io_context.hpp>

#include <stdexcept>
#include <string_view>

#include "nativepg/co_connection_pool.hpp"
#include "test_utils/test_string_contains.hpp"

namespace corosio = boost::corosio;
using namespace nativepg;
using namespace nativepg::test;
using namespace std::chrono_literals;

// Constructing a pool with invalid params throws

namespace {

void check_throws(
    pool_params params,
    std::string_view expected_msg,
    boost::source_location loc = BOOST_CURRENT_LOCATION
)
{
    corosio::io_context ctx;
    try
    {
        static_cast<void>(co_connection_pool(ctx, std::move(params)));
        BOOST_ERROR("Expected std::invalid_argument, but no exception was thrown");
        std::cerr << "  Called from " << loc << std::endl;
    }
    catch (const std::invalid_argument& exc)
    {
        test_string_contains(exc.what(), expected_msg, loc);
    }
};

void test_invalid_params()
{
    check_throws(
        {.transport = {}, .max_total_size = 0},
        "pool_params::max_total_size must be greater than zero"
    );
    check_throws(
        {.transport = {}, .max_total_size = 2, .dedicated = {.initial_size = 4}},
        "pool_params::max_total_size must be greater than or equal to pool_params::dedicated.initial_size"
    );
    check_throws(
        {.transport = {}, .connect_timeout = -1s},
        "pool_params::connect_timeout must not be negative"
    );
    check_throws(
        {.transport = {}, .retry_interval = 0s},
        "pool_params::retry_interval must be greater than zero"
    );
    check_throws(
        {.transport = {}, .retry_interval = -1s},
        "pool_params::retry_interval must be greater than zero"
    );
    check_throws({.transport = {}, .ping_interval = -1s}, "pool_params::ping_interval must not be negative");
    check_throws({.transport = {}, .ping_timeout = -1s}, "pool_params::ping_timeout must not be negative");
}

}  // namespace

int main()
{
    test_invalid_params();

    return boost::report_errors();
}
