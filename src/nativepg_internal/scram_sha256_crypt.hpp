//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_SRC_NATIVEPG_INTERNAL_SCRAM_SHA256_CRYPT_HPP
#define NATIVEPG_SRC_NATIVEPG_INTERNAL_SCRAM_SHA256_CRYPT_HPP

#include <boost/system/detail/error_code.hpp>
#include <boost/system/error_code.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Functions to compute the values used by SCRAM-SHA256
//  SaltedPassword  := Hi(Normalize(password), salt, i)
//  ClientKey       := HMAC(SaltedPassword, "Client Key")
//  StoredKey       := H(ClientKey)
//  AuthMessage     := client-first-message-bare + "," +
//                     server-first-message + "," +
//                     client-final-message-without-proof
//  ClientSignature := HMAC(StoredKey, AuthMessage)
//  ClientProof     := ClientKey XOR ClientSignature
//  ServerKey       := HMAC(SaltedPassword, "Server Key")
//  ServerSignature := HMAC(ServerKey, AuthMessage)

namespace nativepg::protocol::scram_sha256 {

using sha256_digest = std::array<unsigned char, 32u>;

// Tries to apply the StringPrep algorithm with the SASLPrep profile to input.
[[nodiscard]] boost::system::error_code sasl_prep(std::string_view input, std::string& output);

// Prepares a password by either applying sasl_prep, or leaving it as-is
void normalize_password(std::string_view input, std::string& output);

// SaltedPassword
[[nodiscard]] sha256_digest salt_password(
    std::string_view normalized_password,
    std::span<const unsigned char> salt,
    std::uint32_t iteration_count
);

// ClientKey
[[nodiscard]] sha256_digest compute_client_key(const sha256_digest& salted_password);

// StoredKey
[[nodiscard]] sha256_digest compute_stored_key(const sha256_digest& client_key);

// ClientSignature
[[nodiscard]] sha256_digest compute_client_signature(
    const sha256_digest& stored_key,
    std::span<const unsigned char> auth_msg
);

// ClientProof
[[nodiscard]] sha256_digest compute_client_proof(
    const sha256_digest& client_key,
    const sha256_digest& client_signature
);

// ServerKey
[[nodiscard]] sha256_digest compute_server_key(const sha256_digest& salted_password);

// ServerSignature
[[nodiscard]] sha256_digest compute_server_signature(
    const sha256_digest& server_key,
    std::span<const unsigned char> auth_msg
);

// Nonces are 18 bytes of binary output, then base64 encoded
[[nodiscard]] boost::system::error_code generate_nonce(std::string& to);

}  // namespace nativepg::protocol::scram_sha256

#endif
