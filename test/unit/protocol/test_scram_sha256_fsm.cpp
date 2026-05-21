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
#include "test_utils.hpp"

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
    // Test case taken from the SCRAM-SHA256 spec
    // https://datatracker.ietf.org/doc/html/rfc7677
    //    C: n,,n=user,r=rOprNGfwEbeRWgbNEkqO
    //    S: r=rOprNGfwEbeRWgbNEkqO%hvYDpWUa2RaTCAfuxFIlj)hNlF$k0,
    //       s=W22ZaJ0SNY7soEsUEjb6gQ==,i=4096
    //    C: c=biws,r=rOprNGfwEbeRWgbNEkqO%hvYDpWUa2RaTCAfuxFIlj)hNlF$k0,
    //       p=dHzbZapWIk4jUhN+Ute9ytag9zjfMHgsqmmiz7AndVQ=
    //    S: v=6rriTRBi23WpRR/wtup+mMhUZUn/dB5nLTJRsjl95G4=
    constexpr std::string_view password = "pencil";
    constexpr std::string_view client_first = "n,,n=user,r=rOprNGfwEbeRWgbNEkqO";
    constexpr std::string_view server_first =
        "r=rOprNGfwEbeRWgbNEkqO%hvYDpWUa2RaTCAfuxFIlj)hNlF$k0,s=W22ZaJ0SNY7soEsUEjb6gQ==,i=4096";
    constexpr std::string_view client_final =
        "c=biws,r=rOprNGfwEbeRWgbNEkqO%hvYDpWUa2RaTCAfuxFIlj)hNlF$k0,p=dHzbZapWIk4jUhN+"
        "Ute9ytag9zjfMHgsqmmiz7AndVQ=";
    constexpr std::string_view server_final = "v=6rriTRBi23WpRR/wtup+mMhUZUn/dB5nLTJRsjl95G4=";

    // Serialized messages are prefixed by a header (password message and length)
    constexpr unsigned char client_first_len = client_final.size();
    constexpr unsigned char client_final_len = server_final.size();

    std::vector<unsigned char> expected_client_first = {'p', 0, 0, 0, client_first_len};
    expected_client_first.insert(expected_client_first.end(), client_first.begin(), client_first.end());

    std::vector<unsigned char> expected_client_final = {'p', 0, 0, 0, client_final_len};
    expected_client_final.insert(expected_client_final.end(), client_final.begin(), client_final.end());

    // Setup
    std::vector<unsigned char> write_buffer;
    scram_sha256_fsm fsm;
    auto client_nonce_gen = [](std::string& s) -> boost::system::error_code {
        s = "rOprNGfwEbeRWgbNEkqO";
        return {};
    };

    // on_init writes the client-first message
    auto ec = fsm.on_init(client_nonce_gen, write_buffer);
    BOOST_TEST_EQ(ec, error_code());
    NATIVEPG_TEST_CONT_EQ(write_buffer, expected_client_first)

    // on_server_first checks the server-first-message and writes the client-final-message
    ec = fsm.on_server_first(to_span(server_first), password, write_buffer);
    BOOST_TEST_EQ(ec, error_code());
    NATIVEPG_TEST_CONT_EQ(write_buffer, expected_client_final)

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
