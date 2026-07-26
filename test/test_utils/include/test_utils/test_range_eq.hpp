//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_TEST_RANGE_EQ_HPP
#define NATIVEPG_TEST_RANGE_EQ_HPP

#include <boost/assert/source_location.hpp>
#include <boost/core/lightweight_test.hpp>

#include <iostream>
#include <iterator>

namespace nativepg::test {

template <class Range1, class Range2>
bool test_range_eq(Range1&& rng1, Range2&& rng2, boost::source_location loc = BOOST_CURRENT_LOCATION)
{
    bool ok = BOOST_TEST_ALL_EQ(std::begin(rng1), std::end(rng1), std::begin(rng2), std::end(rng2));
    if (!ok)
        std::cerr << "  Called from " << loc << std::endl;
    return ok;
}

}  // namespace nativepg::test

#endif
