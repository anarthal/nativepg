//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_CO_CONNECTION_UTILS_HPP
#define NATIVEPG_CO_CONNECTION_UTILS_HPP

#include <boost/capy/task.hpp>

#include "nativepg/co_connection.hpp"
#include "nativepg/connect_params.hpp"
#include "nativepg/request.hpp"
#include "nativepg/responses/check.hpp"
#include "nativepg/responses/response_handler.hpp"
#include "test_utils/ci_server.hpp"

namespace nativepg::test {

// Establish a connection and returns it.
// Calls std::terminate() on failure. Use only for tests where
// having a working connection is a absolutely required
boost::capy::task<co_connection> establish_connection(
    const connect_params& params = default_connect_params(),
    boost::source_location loc = BOOST_CURRENT_LOCATION
);

boost::capy::task<bool> checked_exec(
    co_connection& conn,
    const request& req,
    response_handler_ref handler,
    boost::source_location loc = BOOST_CURRENT_LOCATION
);

template <response_handler Handler = check>
boost::capy::task<bool> checked_exec(
    co_connection& conn,
    const request& req,
    Handler handler = check(),
    boost::source_location loc = BOOST_CURRENT_LOCATION
)
{
    co_return co_await checked_exec(conn, req, &handler, loc);
}

// Runs a plain request and checks it produces its own response (detects de-syncs)
boost::capy::task<void> check_connection_usable(
    co_connection& conn,
    boost::source_location loc = BOOST_CURRENT_LOCATION
);

}  // namespace nativepg::test

#endif
