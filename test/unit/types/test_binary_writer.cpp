//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <string_view>
#include <vector>
#include <array>
#include <cstddef>

#include <boost/core/lightweight_test.hpp>
#include <boost/core/span.hpp>
#include <boost/system/error_code.hpp>


#include "test_utils.hpp"
#include "nativepg/types/binary_writer.hpp"

using boost::system::error_code;
using nativepg::types::binary_writer;
using nativepg::types::pg_oid_type;


// Some test cases have been copied from CPython's and Android's base64 test suites

namespace {

void test_binary_writer_bool_oid()
{
    // Setup
    bool value = true;
    std::array<std::byte, 1> arr;

    // Encode / Write
    binary_writer<bool, pg_oid_type::bool_oid>::write(value, arr.begin());

    // Check
    constexpr unsigned char expected[] = {
        0x01
    };

    auto actual = {
        static_cast<unsigned char>(arr[0]),
    };

    NATIVEPG_TEST_CONT_EQ(actual, expected)
}

void test_binary_writer_int2()
{
    // Setup
    std::int16_t value = 21;
    std::array<std::byte, 2> arr;

    // Encode / Write
    binary_writer<std::int16_t, pg_oid_type::int2>::write(value, arr.begin());

    // Check
    constexpr unsigned char expected[] = {
        0x15, 0x0
    };
    std::vector<unsigned char> actual;

    if (std::endian::native == std::endian::little)
    {
        // Little endian
        actual = {
            static_cast<unsigned char>(arr[1]),
            static_cast<unsigned char>(arr[0]),
        };
    }
    else
    {
        // Big endian
        actual = {
            static_cast<unsigned char>(arr[0]),
            static_cast<unsigned char>(arr[1]),
        };
    }

    NATIVEPG_TEST_CONT_EQ(actual, expected)
}

}


int main()
{
    test_binary_writer_bool_oid();
    test_binary_writer_int2();

    return boost::report_errors();
}