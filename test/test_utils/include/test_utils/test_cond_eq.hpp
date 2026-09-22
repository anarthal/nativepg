//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_TEST_COND_EQ_HPP
#define NATIVEPG_TEST_COND_EQ_HPP

#include <boost/assert/source_location.hpp>
#include <boost/core/lightweight_test.hpp>

#include <iostream>
#include <iterator>
#include <system_error>

namespace nativepg::test {

inline bool test_cond_eq(
    std::error_code ec,
    std::error_condition cond,
    boost::source_location loc = BOOST_CURRENT_LOCATION
)
{
    bool ok = BOOST_TEST(ec == cond);
    if (!ok)
    {
        std::cerr << "  With ec=" << ec << ": " << ec.message() << "\n";
        std::cerr << "  With cond=" << cond.category().name() << ":" << cond.value() << ": " << cond.message()
                  << '\n';
        std::cerr << "  Called from " << loc << std::endl;
    }
    return ok;
}

}  // namespace nativepg::test

#endif
