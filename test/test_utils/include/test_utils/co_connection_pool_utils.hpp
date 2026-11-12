//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_CO_CONNECTION_POOL_UTILS_HPP
#define NATIVEPG_CO_CONNECTION_POOL_UTILS_HPP

#include <boost/capy/task.hpp>

#include "nativepg/co_connection_pool.hpp"

namespace nativepg::test {

// Utility to run a test that requires a running pool
boost::capy::task<> with_pool_running(co_connection_pool& pool, boost::capy::task<> body);

}  // namespace nativepg::test

#endif
