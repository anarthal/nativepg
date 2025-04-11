//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/core/lightweight_test.hpp>
#include <boost/core/span.hpp>
#include <boost/system/error_code.hpp>

#include <string_view>

#include "nativepg/protocol/scram_sha256.hpp"
#include "test_utils.hpp"

using boost::system::error_code;
using namespace nativepg::protocol;

namespace {

// TODO: more cases and error cases

void test_parse()
{
    // Setup
    constexpr std::string_view
        data = "r=7vha5bhElx564U6mzXimIJqdygCr/dQmx9ESrL/+FfZHVXyA,s=M8SSqYCQ4spIf9DBNNLBJA==,i=4096";
    boost::span<const unsigned char> buff(reinterpret_cast<const unsigned char*>(data.data()), data.size());
    scram_sha256_server_first_message msg{};
    constexpr unsigned char expected_salt[] =
        {0x33, 0xc4, 0x92, 0xa9, 0x80, 0x90, 0xe2, 0xca, 0x48, 0x7f, 0xd0, 0xc1, 0x34, 0xd2, 0xc1, 0x24};

    // Parse
    auto ec = parse(buff, msg);

    // Check
    NATIVEPG_TEST_EQ(ec, error_code())
    NATIVEPG_TEST_EQ(msg.nonce, "7vha5bhElx564U6mzXimIJqdygCr/dQmx9ESrL/+FfZHVXyA")
    NATIVEPG_TEST_CONT_EQ(msg.salt, expected_salt)
    NATIVEPG_TEST_EQ(msg.iteration_count, 4096u)
}

}  // namespace

int main()
{
    test_parse();

    return boost::report_errors();
}