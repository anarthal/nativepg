//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_TEST_STRING_CONTAINS_HPP
#define NATIVEPG_TEST_STRING_CONTAINS_HPP

#include <boost/assert/source_location.hpp>

#include <string_view>

namespace nativepg::test {

// Checks that big contains small, printing both if it doesn't
bool test_string_contains(
    std::string_view big,
    std::string_view small,
    boost::source_location loc = BOOST_CURRENT_LOCATION
);

}  // namespace nativepg::test

#endif
