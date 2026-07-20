//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_TEST_TEST_UTILS_COROSIO_UTILS_HPP
#define NATIVEPG_TEST_TEST_UTILS_COROSIO_UTILS_HPP

#include <boost/assert/source_location.hpp>
#include <boost/capy/task.hpp>

namespace nativepg::test {

// Runs a coroutine-based test to completion on a fresh io_context, failing the test if it doesn't
// finish within a fixed timeout (so a hung test reports a failure instead of blocking forever).
void run_coroutine_test(boost::capy::task<void> test, boost::source_location loc = BOOST_CURRENT_LOCATION);

}  // namespace nativepg::test

#endif
