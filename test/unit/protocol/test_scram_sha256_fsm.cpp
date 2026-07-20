//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/core/lightweight_test.hpp>
#include <boost/system/error_code.hpp>

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "nativepg/protocol/detail/scram_sha256_fsm.hpp"
#include "test_utils/test_utils.hpp"

using boost::system::error_code;
using nativepg::protocol::detail::scram_sha256_fsm;

namespace {

// TODO: errors
// TODO: empty password

std::span<const unsigned char> to_span(std::string_view s)
{
    return {reinterpret_cast<const unsigned char*>(s.data()), s.size()};
}

void test_success()
{
    // Test case extracted by Wireshark snooping
    // Client messages contain the header and additional fields and are shown as binary
    // Server messages are stripped from the header by the higher level, and are shown as text
    constexpr std::string_view password = "secret";

    constexpr unsigned char client_first[] = {
        0x70, 0x00, 0x00, 0x00, 0x36, 0x53, 0x43, 0x52, 0x41, 0x4d, 0x2d, 0x53, 0x48, 0x41,
        0x2d, 0x32, 0x35, 0x36, 0x00, 0x00, 0x00, 0x00, 0x20, 0x6e, 0x2c, 0x2c, 0x6e, 0x3d,
        0x2c, 0x72, 0x3d, 0x70, 0x5a, 0x42, 0x35, 0x34, 0x34, 0x6e, 0x75, 0x53, 0x72, 0x58,
        0x36, 0x72, 0x4f, 0x45, 0x58, 0x39, 0x52, 0x6d, 0x30, 0x43, 0x6d, 0x38, 0x50,
    };
    constexpr std::string_view
        server_first = "r=pZB544nuSrX6rOEX9Rm0Cm8P4tpEwcWmNQ2Ka9mAgbArOGoq,s=z5vnwGM4IDVpGmj1QcWtNA==,i=4096";

    constexpr unsigned char client_final[] = {
        0x70, 0x00, 0x00, 0x00, 0x6c, 0x63, 0x3d, 0x62, 0x69, 0x77, 0x73, 0x2c, 0x72, 0x3d, 0x70, 0x5a,
        0x42, 0x35, 0x34, 0x34, 0x6e, 0x75, 0x53, 0x72, 0x58, 0x36, 0x72, 0x4f, 0x45, 0x58, 0x39, 0x52,
        0x6d, 0x30, 0x43, 0x6d, 0x38, 0x50, 0x34, 0x74, 0x70, 0x45, 0x77, 0x63, 0x57, 0x6d, 0x4e, 0x51,
        0x32, 0x4b, 0x61, 0x39, 0x6d, 0x41, 0x67, 0x62, 0x41, 0x72, 0x4f, 0x47, 0x6f, 0x71, 0x2c, 0x70,
        0x3d, 0x43, 0x53, 0x39, 0x54, 0x61, 0x4a, 0x74, 0x68, 0x77, 0x46, 0x33, 0x5a, 0x39, 0x57, 0x4a,
        0x71, 0x6b, 0x37, 0x63, 0x44, 0x4b, 0x36, 0x5a, 0x76, 0x42, 0x35, 0x31, 0x70, 0x32, 0x66, 0x32,
        0x64, 0x6d, 0x63, 0x48, 0x50, 0x2f, 0x6d, 0x45, 0x66, 0x77, 0x51, 0x77, 0x3d,
    };

    constexpr std::string_view server_final = "v=lokWerupArzAF0lzq7rCyQGcj8nFxsECGTFbbRLVO20=";

    // Setup
    std::vector<unsigned char> write_buffer{0xff, 0xff};  // previous contents
    scram_sha256_fsm fsm;
    auto client_nonce_gen = [](std::string& s) -> boost::system::error_code {
        s = "pZB544nuSrX6rOEX9Rm0Cm8P";
        return {};
    };

    // on_init writes the client-first message
    auto ec = fsm.on_init(client_nonce_gen, write_buffer);
    BOOST_TEST_EQ(ec, error_code());
    NATIVEPG_TEST_CONT_EQ(write_buffer, client_first)

    // on_server_first checks the server-first-message and writes the client-final-message
    ec = fsm.on_server_first(to_span(server_first), password, write_buffer);
    BOOST_TEST_EQ(ec, error_code());
    NATIVEPG_TEST_CONT_EQ(write_buffer, client_final)

    // on_server_final checks the server-final-message
    ec = fsm.on_server_final(to_span(server_final));
    BOOST_TEST_EQ(ec, error_code());
}

}  // namespace

int main()
{
    test_success();

    return boost::report_errors();
}
