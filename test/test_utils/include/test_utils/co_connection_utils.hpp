//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_CO_CONNECTION_UTILS_HPP
#define NATIVEPG_CO_CONNECTION_UTILS_HPP

#include <boost/capy/task.hpp>

#include <optional>
#include <string>
#include <string_view>

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

template <response_handler Handler>
boost::capy::task<bool> checked_exec(
    co_connection& conn,
    const request& req,
    Handler handler,
    boost::source_location loc = BOOST_CURRENT_LOCATION
)
{
    co_return co_await checked_exec(conn, req, &handler, loc);
}

inline boost::capy::task<bool> checked_exec(
    co_connection& conn,
    const request& req,
    boost::source_location loc = BOOST_CURRENT_LOCATION
)
{
    return checked_exec(conn, req, check(), loc);
}

inline boost::capy::task<bool> checked_exec(
    co_connection& conn,
    std::string_view sql,
    boost::source_location loc = BOOST_CURRENT_LOCATION
)
{
    request req;
    req.add_query(sql);
    co_return co_await checked_exec(conn, req, check(), loc);
}

// Runs a plain request and checks it produces its own response (detects de-syncs)
boost::capy::task<bool> check_connection_usable(
    co_connection& conn,
    boost::source_location loc = BOOST_CURRENT_LOCATION
);

// Postgres accepts settings under an arbitrary prefix, and keeps them for as long
// as the session lives. We use them to mark sessions.

// The SQL that set_session_var runs. Exposed for tests that need to drive the
// request themselves, rather than just executing it
std::string set_session_var_query(std::string_view value);

// Sets the session variable, failing the test if the query errors
boost::capy::task<bool> set_session_var(
    co_connection& conn,
    std::string_view value,
    boost::source_location loc = BOOST_CURRENT_LOCATION
);

// Reads the session variable back. Returns an empty optional if it was never set
boost::capy::task<std::optional<std::string>> get_session_var(
    co_connection& conn,
    boost::source_location loc = BOOST_CURRENT_LOCATION
);

}  // namespace nativepg::test

#endif
