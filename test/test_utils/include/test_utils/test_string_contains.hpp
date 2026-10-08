//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_TEST_STRING_CONTAINS_HPP
#define NATIVEPG_TEST_STRING_CONTAINS_HPP

#include <boost/assert/source_location.hpp>
#include <boost/core/lightweight_test.hpp>

#include <iostream>
#include <string_view>

namespace nativepg::test {

inline bool test_string_contains(
    std::string_view big,
    std::string_view small,
    boost::source_location loc = BOOST_CURRENT_LOCATION
)
{
    constexpr auto pred = [](std::string_view lhs, std::string_view rhs) {
        return lhs.find(rhs) != std::string_view::npos;
    };
    bool ok = BOOST_TEST_WITH(big, small, pred);
    if (!ok)
        std::cerr << "  Called from " << loc << std::endl;
    return ok;
}

}  // namespace nativepg::test

#endif
