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
    constexpr std::string_view data = "v=N9rueOuELVCa2VUm1hdWi5PpRrLafRO0j2lRL312E2k=";
    boost::span<const unsigned char> buff(reinterpret_cast<const unsigned char*>(data.data()), data.size());
    scram_sha256_server_final_message msg{};
    constexpr unsigned char expected_server_signature[] = {
        0x37, 0xda, 0xee, 0x78, 0xeb, 0x84, 0x2d, 0x50, 0x9a, 0xd9, 0x55, 0x26, 0xd6, 0x17, 0x56, 0x8b,
        0x93, 0xe9, 0x46, 0xb2, 0xda, 0x7d, 0x13, 0xb4, 0x8f, 0x69, 0x51, 0x2f, 0x7d, 0x76, 0x13, 0x69,
    };

    // Parse
    auto ec = parse(buff, msg);

    // Check
    NATIVEPG_TEST_EQ(ec, error_code())
    NATIVEPG_TEST_CONT_EQ(msg.server_signature, expected_server_signature)
}

}  // namespace

int main()
{
    test_parse();

    return boost::report_errors();
}