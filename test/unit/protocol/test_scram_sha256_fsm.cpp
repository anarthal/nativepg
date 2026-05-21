//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/core/lightweight_test.hpp>
#include <boost/system/detail/error_code.hpp>
#include <boost/system/error_code.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "nativepg/protocol/detail/scram_sha256_fsm.hpp"
#include "nativepg_internal/base64.hpp"
#include "nativepg_internal/scram_sha256_crypt.hpp"
#include "test_utils.hpp"

using nativepg::protocol::detail::base64_encode;
using nativepg::protocol::detail::scram_sha256_fsm;
using namespace nativepg::protocol::detail::scram_sha256;
using boost::system::error_code;

namespace {

// TODO: errors
// TODO: empty password

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

    // Recover the FSM's generated client nonce from the buffer. The
    // client-first-message-bare ends the buffer and starts with "n=,r=",
    // so everything after that marker up to the end is the nonce.
    std::string_view init_sv(reinterpret_cast<const char*>(write_buffer.data()), write_buffer.size());
    auto bare_offset = init_sv.find("n=,r=");
    BOOST_TEST(bare_offset != std::string_view::npos);
    const std::string client_nonce(init_sv.substr(bare_offset + 5));

    // Synthesize the server-first message using the FSM's nonce
    const std::string server_first = "r=" + client_nonce + std::string(server_nonce_extension) +
                                     ",s=" + std::string(salt_b64) + ",i=4096";
    const std::span<const unsigned char> server_first_bytes(
        reinterpret_cast<const unsigned char*>(server_first.data()),
        server_first.size()
    );

    std::vector<unsigned char> client_final_buf;
    ec = fsm.on_server_first(server_first_bytes, password, client_final_buf);
    BOOST_TEST_EQ(ec, error_code());

    // Compute what the FSM should have produced via compute_proofs. The
    // AuthMessage is client-first-bare + "," + server-first + "," +
    // client-final-without-proof. With no channel binding, the bare
    // client-final-without-proof is "c=biws,r=<full_nonce>".
    const std::string full_nonce = client_nonce + std::string(server_nonce_extension);
    const std::string auth_msg = "n=,r=" + client_nonce + "," + server_first + "," + "c=biws,r=" + full_nonce;

    sha256_digest expected_proof{};
    sha256_digest expected_signature{};
    ec = compute_proofs(
        password,
        salt,
        iteration_count,
        {reinterpret_cast<const unsigned char*>(auth_msg.data()), auth_msg.size()},
        expected_proof,
        expected_signature
    );
    BOOST_TEST_EQ(ec, error_code());

    // Hand the FSM a server-final built with the expected signature. If the
    // FSM stored the right server_signature during on_server_first, this
    // verification step must succeed.
    std::vector<unsigned char> sig_b64;
    base64_encode(expected_signature, sig_b64);
    const std::string server_final = "v=" + std::string(sig_b64.begin(), sig_b64.end());
    const std::span<const unsigned char> server_final_bytes(
        reinterpret_cast<const unsigned char*>(server_final.data()),
        server_final.size()
    );

    ec = fsm.on_server_final(server_final_bytes);
    BOOST_TEST_EQ(ec, error_code());
}

}  // namespace

int main()
{
    test_success();

    return boost::report_errors();
}
