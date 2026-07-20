//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/core/lightweight_test.hpp>
#include <boost/system/error_code.hpp>

#include <string_view>

#include "nativepg_internal/scram_sha256_crypt.hpp"
#include "test_utils/test_utils.hpp"

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

    constexpr unsigned char salt[] =
        {0x5b, 0x6d, 0x99, 0x68, 0x9d, 0x12, 0x35, 0x8e, 0xec, 0xa0, 0x4b, 0x14, 0x12, 0x36, 0xfa, 0x81};

    constexpr std::string_view auth_message =
        "n=user,r=rOprNGfwEbeRWgbNEkqO"
        ","
        "r=rOprNGfwEbeRWgbNEkqO%hvYDpWUa2RaTCAfuxFIlj)hNlF$k0,s=W22ZaJ0SNY7soEsUEjb6gQ==,i=4096"
        ","
        "c=biws,r=rOprNGfwEbeRWgbNEkqO%hvYDpWUa2RaTCAfuxFIlj)hNlF$k0";

    constexpr unsigned char expected_client_proof[32] = {
        0x74, 0x7c, 0xdb, 0x65, 0xaa, 0x56, 0x22, 0x4e, 0x23, 0x52, 0x13, 0x7e, 0x52, 0xd7, 0xbd, 0xca,
        0xd6, 0xa0, 0xf7, 0x38, 0xdf, 0x30, 0x78, 0x2c, 0xaa, 0x69, 0xa2, 0xcf, 0xb0, 0x27, 0x75, 0x54,
    };

    constexpr unsigned char expected_server_signature[32] = {
        0xea, 0xba, 0xe2, 0x4d, 0x10, 0x62, 0xdb, 0x75, 0xa9, 0x45, 0x1f, 0xf0, 0xb6, 0xea, 0x7e, 0x98,
        0xc8, 0x54, 0x65, 0x49, 0xff, 0x74, 0x1e, 0x67, 0x2d, 0x32, 0x51, 0xb2, 0x39, 0x7d, 0xe4, 0x6e,
    };

    sha256_digest client_proof, server_signature;
    auto ec = compute_proofs(
        "pencil",
        salt,
        4096u,
        {reinterpret_cast<const unsigned char*>(auth_message.data()), auth_message.size()},
        client_proof,
        server_signature
    );

    BOOST_TEST_EQ(ec, error_code());
    NATIVEPG_TEST_CONT_EQ(client_proof, expected_client_proof)
    NATIVEPG_TEST_CONT_EQ(server_signature, expected_server_signature)
}

}  // namespace

int main()
{
    test_success();

    return boost::report_errors();
}