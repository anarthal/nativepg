//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_TEST_TEST_UTILS_COROSIO_UTILS_HPP
#define NATIVEPG_TEST_TEST_UTILS_COROSIO_UTILS_HPP

#include <boost/assert/source_location.hpp>
#include <boost/capy/io_result.hpp>
#include <boost/capy/task.hpp>

#include <system_error>

#include "nativepg/extended_error.hpp"

namespace nativepg::test {

// Runs a coroutine-based test to completion on a fresh io_context, failing the test if it doesn't
// finish within a fixed timeout (so a hung test reports a failure instead of blocking forever).
void run_coroutine_test(boost::capy::task<void> test, boost::source_location loc = BOOST_CURRENT_LOCATION);

// Utilities to check the result of a Capy operation
bool check_success(
    std::error_code ec,
    const diagnostics& diag,
    boost::source_location loc = BOOST_CURRENT_LOCATION
);

inline bool check_success(
    boost::capy::io_result<> res,
    const diagnostics& diag,
    boost::source_location loc = BOOST_CURRENT_LOCATION
)
{
    return check_success(res.ec, diag, loc);
}

}  // namespace nativepg::test

#endif
