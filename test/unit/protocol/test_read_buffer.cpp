//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/core/lightweight_test.hpp>

#include "nativepg/protocol/detail/read_buffer.hpp"

using namespace nativepg;
using protocol::detail::next_power_of_2;
using protocol::detail::read_buffer;

namespace {

void test_next_power_of_2()
{
    BOOST_TEST_EQ(next_power_of_2(0u), 1u);
    BOOST_TEST_EQ(next_power_of_2(1u), 1u);
    BOOST_TEST_EQ(next_power_of_2(2u), 2u);
    BOOST_TEST_EQ(next_power_of_2(3u), 4u);
    BOOST_TEST_EQ(next_power_of_2(4u), 4u);
    BOOST_TEST_EQ(next_power_of_2(5u), 8u);
    BOOST_TEST_EQ(next_power_of_2(6u), 8u);
    BOOST_TEST_EQ(next_power_of_2(7u), 8u);
    BOOST_TEST_EQ(next_power_of_2(8u), 8u);
    BOOST_TEST_EQ(next_power_of_2(9u), 16u);
    BOOST_TEST_EQ(next_power_of_2(10u), 16u);
    BOOST_TEST_EQ(next_power_of_2(15u), 16u);
    BOOST_TEST_EQ(next_power_of_2(16u), 16u);
    BOOST_TEST_EQ(next_power_of_2(17u), 32u);
    BOOST_TEST_EQ(next_power_of_2(1000u), 1024u);
    BOOST_TEST_EQ(next_power_of_2(1024u), 1024u);
    BOOST_TEST_EQ(next_power_of_2(4093u), 4096u);
}

}  // namespace

int main()
{
    test_next_power_of_2();

    return boost::report_errors();
}
