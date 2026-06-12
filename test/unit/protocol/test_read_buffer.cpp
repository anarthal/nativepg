//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/core/lightweight_test.hpp>

#include <cstddef>
#include <limits>

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

    if constexpr (sizeof(std::size_t) == 8u)
    {
        // Values around the 32-bit boundary
        BOOST_TEST_EQ(next_power_of_2(2147483647ull), 2147483648ull);  // 2^31 - 1 -> 2^31
        BOOST_TEST_EQ(next_power_of_2(2147483648ull), 2147483648ull);  // 2^31     -> 2^31
        BOOST_TEST_EQ(next_power_of_2(2147483649ull), 4294967296ull);  // 2^31 + 1 -> 2^32
        BOOST_TEST_EQ(next_power_of_2(2147483650ull), 4294967296ull);  // 2^31 + 2 -> 2^32
        BOOST_TEST_EQ(next_power_of_2(4294967296ull), 4294967296ull);  // 2^32     -> 2^32

        // Values around the 64-bit boundary
        constexpr auto size_max = (std::numeric_limits<std::size_t>::max)();
        BOOST_TEST_EQ(next_power_of_2(9223372036854775806ull), 9223372036854775808ull);  // 2^63 - 2 -> 2^63
        BOOST_TEST_EQ(next_power_of_2(9223372036854775807ull), 9223372036854775808ull);  // 2^63 - 1 -> 2^63
        BOOST_TEST_EQ(next_power_of_2(9223372036854775808ull), 9223372036854775808ull);  // 2^63     -> 2^63
        BOOST_TEST_EQ(next_power_of_2(9223372036854775809ull), size_max);                // 2^63 + 1 -> max
        BOOST_TEST_EQ(next_power_of_2(size_max - 1u), size_max);                         // max - 1  -> max
        BOOST_TEST_EQ(next_power_of_2(size_max), size_max);                              // max      -> max
    }
}

}  // namespace

int main()
{
    test_next_power_of_2();

    return boost::report_errors();
}
