//
// Copyright (c) 2019-2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/mysql/any_connection.hpp>
#include <boost/mysql/character_set.hpp>
#include <boost/mysql/client_errc.hpp>
#include <boost/mysql/connection_pool.hpp>
#include <boost/mysql/diagnostics.hpp>
#include <boost/mysql/error_code.hpp>
#include <boost/mysql/field_view.hpp>
#include <boost/mysql/pool_params.hpp>
#include <boost/mysql/results.hpp>
#include <boost/mysql/ssl_mode.hpp>
#include <boost/mysql/string_view.hpp>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/cancel_after.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/deferred.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/strand.hpp>
#include <boost/test/data/test_case.hpp>
#include <boost/test/unit_test.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include "test_common/ci_server.hpp"
#include "test_common/create_basic.hpp"
#include "test_common/io_context_fixture.hpp"
#include "test_common/network_result.hpp"
#include "test_common/printing.hpp"
#include "test_common/tracker_executor.hpp"
#include "test_integration/run_coro.hpp"
#include "test_integration/server_features.hpp"

using namespace boost::mysql;
using namespace boost::mysql::test;
using boost::test_tools::per_element;
namespace data = boost::unit_test::data;
namespace asio = boost::asio;

BOOST_AUTO_TEST_SUITE(test_connection_pool)

pool_params create_pool_params(std::size_t max_size = 151)
{
    pool_params res;
    res.server_address.emplace_host_and_port(get_hostname());
    res.username = integ_user;
    res.password = integ_passwd;
    res.database = integ_db;
    res.ssl = ssl_mode::disable;
    res.max_size = max_size;
    return res;
}

static void check_run(error_code ec)
{
    // Should complete successfully
    BOOST_TEST(ec == error_code());

    // Should never complete immediately
    BOOST_TEST(!is_initiation_function());
}

struct fixture : io_context_fixture
{
    // async_get_connection actually passes nullptr as diagnostics* to initiation
    // functions if no diagnostics is provided (unlike any_connection)
    diagnostics diag;
    results r;
};

BOOST_DATA_TEST_CASE_F(fixture, return_connection_without_reset, data::make({false, true}))
{
    // Create a connection pool with max_size 1, so the same connection gets always returned
    auto params = create_pool_params(1);
    params.thread_safe = sample;
    connection_pool pool(ctx, std::move(params));
    auto run_result = pool.async_run(as_netresult);

    // Get a connection
    auto conn = pool.async_get_connection(diag, as_netresult).get();

    // Alter session state
    BOOST_TEST_REQUIRE(conn.valid());
    conn->async_execute("SET @myvar = 'abc'", r, as_netresult).validate_no_error();

    // Return the connection
    conn.return_without_reset();
    BOOST_TEST(!conn.valid());

    // Get the same connection again
    conn = pool.async_get_connection(diag, as_netresult).get();

    // The same connection is returned, and no reset has been issued
    BOOST_TEST_REQUIRE(conn.valid());
    conn->async_execute("SELECT @myvar", r, as_netresult).validate_no_error();
    BOOST_TEST(r.rows() == makerows(1, "abc"), per_element());

    // Cleanup the pool
    pool.cancel();
    std::move(run_result).validate_no_error_nodiag();
}

// pooled_connection destructor is equivalent to return_connection with reset
BOOST_FIXTURE_TEST_CASE(pooled_connection_destructor, fixture)
{
    // Create a connection pool with max_size 1, so the same connection gets always returned
    connection_pool pool(ctx, create_pool_params(1));
    auto run_result = pool.async_run(as_netresult);

    {
        // Get a connection
        auto conn = pool.async_get_connection(diag, as_netresult).get();

        // Alter session state
        BOOST_TEST_REQUIRE(conn.valid());
        conn->async_execute("SET @myvar = 'abc'", r, as_netresult).validate_no_error();
    }

    // Get the same connection again
    auto conn = pool.async_get_connection(diag, as_netresult).get();

    // The same connection is returned, but session state has been cleared
    BOOST_TEST_REQUIRE(conn.valid());
    conn->async_execute("SELECT @myvar", r, as_netresult).validate_no_error();
    BOOST_TEST(r.rows() == makerows(1, nullptr), per_element());

    // Cleanup the pool
    pool.cancel();
    std::move(run_result).validate_no_error_nodiag();
}

BOOST_FIXTURE_TEST_CASE(connections_created_if_required, fixture)
{
    connection_pool pool(ctx, create_pool_params());
    auto run_result = pool.async_run(as_netresult);

    // Get a connection
    auto conn1 = pool.async_get_connection(diag, as_netresult).get();

    // Check that it works
    BOOST_TEST_REQUIRE(conn1.valid());
    conn1->async_execute("SET @myvar = '1'", r, as_netresult).validate_no_error();

    // Get another connection. This will create a new one, since the first one is in use
    auto conn2 = pool.async_get_connection(diag, as_netresult).get();

    // Check that it works
    BOOST_TEST_REQUIRE(conn1.valid());
    conn2->async_execute("SET @myvar = '2'", r, as_netresult).validate_no_error();

    // They are different connections
    conn1->async_execute("SELECT @myvar", r, as_netresult).validate_no_error();
    BOOST_TEST(r.rows() == makerows(1, "1"), per_element());
    conn2->async_execute("SELECT @myvar", r, as_netresult).validate_no_error();
    BOOST_TEST(r.rows() == makerows(1, "2"), per_element());

    // Cleanup the pool
    pool.cancel();
    std::move(run_result).validate_no_error_nodiag();
}

std::int64_t get_connection_id(any_connection& conn)
{
    results r;
    conn.async_execute("SELECT CONNECTION_ID()", r, as_netresult).validate_no_error();
    return r.rows().at(0).at(0).as_uint64();
}

// Regression check: https://github.com/boostorg/mysql/issues/395
// If there are more outstanding connection requests than pending connections,
// connections are created
BOOST_FIXTURE_TEST_CASE(connections_created_if_requests_gt_pending, fixture)
{
    connection_pool pool(ctx, create_pool_params());
    auto run_result = pool.async_run(as_netresult);

    // Request several connections in parallel
    auto conn1_result = pool.async_get_connection(diag, as_netresult);
    auto conn2_result = pool.async_get_connection(diag, as_netresult);
    auto conn3_result = pool.async_get_connection(diag, as_netresult);

    // Resolve the requests
    auto conn1 = std::move(conn1_result).get();
    auto conn2 = std::move(conn2_result).get();
    auto conn3 = std::move(conn3_result).get();

    // They should be different connections
    auto conn1_id = get_connection_id(conn1.get());
    auto conn2_id = get_connection_id(conn2.get());
    auto conn3_id = get_connection_id(conn3.get());
    BOOST_TEST(conn1_id != conn2_id);
    BOOST_TEST(conn1_id != conn3_id);

    // Cleanup the pool
    pool.cancel();
    std::move(run_result).validate_no_error_nodiag();
}

BOOST_FIXTURE_TEST_CASE(connection_upper_limit, fixture)
{
    connection_pool pool(ctx, create_pool_params(1));
    auto run_result = pool.async_run(as_netresult);

    // Get a connection
    auto conn = pool.async_get_connection(diag, as_netresult).get();

    // Getting another connection will block until one is returned.
    // Since we won't return the one we have, the function time outs
    pool.async_get_connection(diag, asio::cancel_after(std::chrono::milliseconds(1), asio::deferred))(
            as_netresult
    )
        .validate_error(client_errc::no_connection_available);

    // Cleanup the pool
    pool.cancel();
    std::move(run_result).validate_no_error_nodiag();
}

// If a connection is requested before calling run, we wait
BOOST_DATA_TEST_CASE_F(fixture, get_connection_before_run, data::make({false, true}))
{
    auto params = create_pool_params(151);
    params.thread_safe = sample;
    connection_pool pool(ctx, std::move(params));

    // Get some connections before calling run
    auto getconn1_result = pool.async_get_connection(diag, as_netresult);
    auto getconn2_result = pool.async_get_connection(diag, as_netresult);

    // Call run
    auto run_result = pool.async_run(as_netresult);

    // Success
    auto conn1 = std::move(getconn1_result).get();
    auto conn2 = std::move(getconn2_result).get();

    // They're different connections
    BOOST_TEST(get_connection_id(conn1.get()) != get_connection_id(conn2.get()));

    // Cleanup the pool
    pool.cancel();
    std::move(run_result).validate_no_error_nodiag();
}

BOOST_FIXTURE_TEST_CASE(cancel_run, fixture)
{
    // Construct a pool and run it
    connection_pool pool(ctx, create_pool_params());
    auto run_result = pool.async_run(as_netresult);

    // Get a connection
    auto conn = pool.async_get_connection(diag, as_netresult).get();

    // Cancel. This will make run() return
    pool.cancel();
    std::move(run_result).validate_no_error_nodiag();

    // Cancel again does nothing
    pool.cancel();
}

// If the pool is cancelled before calling run, cancel still has effect
BOOST_FIXTURE_TEST_CASE(cancel_before_run, fixture)
{
    // Create a pool
    connection_pool pool(ctx, create_pool_params());

    // Cancel
    pool.cancel();

    // Run returns immediately
    pool.async_run(as_netresult).validate_no_error_nodiag();
}

BOOST_DATA_TEST_CASE_F(fixture, cancel_get_connection, data::make({false, true}))
{
    // Construct a pool and run it
    auto params = create_pool_params(1);
    params.thread_safe = sample;
    connection_pool pool(ctx, std::move(params));
    auto run_result = pool.async_run(as_netresult);

    // Get a connection
    auto conn = pool.async_get_connection(diag, as_netresult).get();

    // Try to get a new one. This will not complete, since there is no room for more connections
    diagnostics diag2;
    auto getconn_result = pool.async_get_connection(diag2, as_netresult);

    // Cancel. This will make run and get_connection return
    pool.cancel();
    std::move(run_result).validate_no_error_nodiag();
    std::move(getconn_result).validate_error(client_errc::pool_cancelled);

    // Calling get_connection after cancel will error
    pool.async_get_connection(diag, as_netresult).validate_error(client_errc::pool_cancelled);
}

// Spotcheck: the pool can work with zero timeouts
BOOST_FIXTURE_TEST_CASE(zero_timeouts, fixture)
{
    // Create and run the pool
    auto params = create_pool_params();
    params.max_size = 1u;  // so we force a reset
    params.connect_timeout = std::chrono::seconds(0);
    params.ping_timeout = std::chrono::seconds(0);
    params.ping_interval = std::chrono::seconds(0);
    connection_pool pool(ctx, std::move(params));
    auto run_result = pool.async_run(as_netresult);

    // Get a connection
    auto conn = pool.async_get_connection(diag, as_netresult).get();
    conn->async_ping(as_netresult).validate_no_error();

    // Cleanup the pool
    pool.cancel();
    std::move(run_result).validate_no_error_nodiag();
}

// Spotcheck: constructing a connection_pool with invalid params throws
BOOST_AUTO_TEST_CASE(invalid_params)
{
    asio::io_context ctx;
    pool_params params;
    params.connect_timeout = std::chrono::seconds(-1);

    BOOST_CHECK_EXCEPTION(
        connection_pool(ctx, std::move(params)),
        std::invalid_argument,
        [](const std::invalid_argument& exc) {
            return exc.what() == string_view("pool_params::connect_timeout must not be negative");
        }
    );
}

BOOST_AUTO_TEST_SUITE_END()
