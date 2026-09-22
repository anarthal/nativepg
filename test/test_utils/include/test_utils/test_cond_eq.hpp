//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_TEST_COND_EQ_HPP
#define NATIVEPG_TEST_COND_EQ_HPP

#include <boost/assert/source_location.hpp>

#include <system_error>

namespace nativepg::test {

// Checks that an error_code matches an error_condition, printing both if it doesn't
bool test_cond_eq(
    std::error_code ec,
    std::error_condition cond,
    boost::source_location loc = BOOST_CURRENT_LOCATION
);

}  // namespace nativepg::test

#endif
