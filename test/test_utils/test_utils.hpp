//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_TEST_TEST_UTILS_TEST_UTILS_HPP
#define NATIVEPG_TEST_TEST_UTILS_TEST_UTILS_HPP

#include <boost/assert/source_location.hpp>
#include <boost/core/lightweight_test.hpp>

#include <string_view>

namespace nativepg {
namespace test {

struct context_frame
{
    context_frame(boost::source_location loc = BOOST_CURRENT_LOCATION) : context_frame({}, loc) {}
    context_frame(std::string_view message, boost::source_location loc = BOOST_CURRENT_LOCATION);
    context_frame(const context_frame&) = delete;
    context_frame(context_frame&&) = delete;
    context_frame& operator=(const context_frame&) = delete;
    context_frame& operator=(context_frame&&) = delete;
    ~context_frame();
};

void print_context();

}  // namespace test
}  // namespace nativepg

#define NATIVEPG_TEST(a) \
    if (!BOOST_TEST(a))  \
        ::nativepg::test::print_context();

#define NATIVEPG_TEST_CONT_EQ(a, b)                                                         \
    if (!BOOST_TEST_ALL_EQ(::std::begin(a), ::std::end(a), ::std::begin(b), ::std::end(b))) \
        ::nativepg::test::print_context();

#define NATIVEPG_TEST_EQ(a, b) \
    if (!BOOST_TEST_EQ(a, b))  \
        ::nativepg::test::print_context();

#endif
