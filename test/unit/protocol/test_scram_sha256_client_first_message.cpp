//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/core/lightweight_test.hpp>
#include <boost/system/detail/error_code.hpp>
#include <boost/system/error_code.hpp>

#include <string_view>
#include <vector>

#include "nativepg/protocol/scram_sha256.hpp"
#include "test_utils.hpp"

using boost::system::error_code;
using namespace nativepg::protocol;

namespace {

// TODO: more cases?

void test_serialize()
{
    // Setup
    std::vector<unsigned char> buff{0xff, 0xff};
    scram_sha256_client_first_message msg{"SCRAM-SHA-256", "7vha5bhElx564U6mzXimIJqd"};
    constexpr unsigned char expected[] = {
        0xff, 0xff, 0x70, 0x00, 0x00, 0x00, 0x36, 0x53, 0x43, 0x52, 0x41, 0x4d, 0x2d, 0x53, 0x48,
        0x41, 0x2d, 0x32, 0x35, 0x36, 0x00, 0x00, 0x00, 0x00, 0x20, 0x6e, 0x2c, 0x2c, 0x6e, 0x3d,
        0x2c, 0x72, 0x3d, 0x37, 0x76, 0x68, 0x61, 0x35, 0x62, 0x68, 0x45, 0x6c, 0x78, 0x35, 0x36,
        0x34, 0x55, 0x36, 0x6d, 0x7a, 0x58, 0x69, 0x6d, 0x49, 0x4a, 0x71, 0x64,
    };

    // Serialize
    auto ec = serialize(msg, buff);

    // Check
    NATIVEPG_TEST_EQ(ec, error_code())
    NATIVEPG_TEST_CONT_EQ(buff, expected)
}

}  // namespace

int main()
{
    test_serialize();

    return boost::report_errors();
}