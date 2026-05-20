//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_SRC_NATIVEPG_INTERNAL_SCRAM_SHA256_CRYPT_HPP
#define NATIVEPG_SRC_NATIVEPG_INTERNAL_SCRAM_SHA256_CRYPT_HPP

#include <boost/assert.hpp>
#include <boost/endian/conversion.hpp>
#include <boost/system/error_code.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <openssl/core_names.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/params.h>
#include <openssl/rand.h>
#include <openssl/types.h>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "nativepg_internal/base64.hpp"
#include "nativepg_internal/openssl_error.hpp"

// Functions to compute the values used by SCRAM-SHA256

namespace nativepg::protocol::detail::scram_sha256 {

inline std::span<const unsigned char> to_span(std::string_view from)
{
    return {reinterpret_cast<const unsigned char*>(from.data()), from.size()};
}

using sha256_digest = std::array<unsigned char, 32u>;

// Tries to apply the StringPrep algorithm with the SASLPrep profile to input.
// TODO: this is a complex algorithm and is not implemented yet.
// It should be valid for ASCII passwords, at least
[[nodiscard]] inline boost::system::error_code sasl_prep(std::string_view input, std::string& output)
{
    output = input;
    return {};
}

[[nodiscard]]
inline sha256_digest compute_xor(const sha256_digest& lhs, const sha256_digest& rhs)
{
    sha256_digest result;
    for (std::size_t i = 0; i < result.size(); ++i)
        result[i] = lhs[i] ^ rhs[i];
    return result;
}

// Prepares a password by either applying sasl_prep, or leaving it as-is
inline void normalize_password(std::string_view input, std::string& output)
{
    // Try to do SASLPrep.
    // If that fails, use the password as-is (as per the postgres spec)
    if (auto ec = sasl_prep(input, output))
        output = input;
}

// One-shot HMAC computation, but re-using a context.
// The context must have been properly configured.
[[nodiscard]] inline boost::system::error_code compute_hmac(
    EVP_MAC_CTX* ctx,
    std::span<const unsigned char> key,
    std::span<const unsigned char> data,
    sha256_digest& output
)
{
    std::size_t outlen = 0;
    if (!EVP_MAC_init(ctx, key.data(), key.size(), nullptr) ||
        !EVP_MAC_update(ctx, data.data(), data.size()) ||
        !EVP_MAC_final(ctx, output.data(), &outlen, output.size()))
    {
        return ::nativepg::detail::translate_openssl_error(ERR_get_error());
    }
    BOOST_ASSERT(outlen == output.size());  // Output is fixed-size
    return {};
}

// Variant with two data pieces
[[nodiscard]] inline boost::system::error_code compute_hmac(
    EVP_MAC_CTX* ctx,
    std::span<const unsigned char> key,
    std::span<const unsigned char> data1,
    std::span<const unsigned char> data2,
    sha256_digest& output
)
{
    std::size_t outlen = 0;
    if (!EVP_MAC_init(ctx, key.data(), key.size(), nullptr) ||
        !EVP_MAC_update(ctx, data1.data(), data1.size()) ||
        !EVP_MAC_update(ctx, data2.data(), data2.size()) ||
        !EVP_MAC_final(ctx, output.data(), &outlen, output.size()))
    {
        return ::nativepg::detail::translate_openssl_error(ERR_get_error());
    }
    BOOST_ASSERT(outlen == output.size());  // output is fixed size
    return {};
}

//  Aux Hi
[[nodiscard]] inline boost::system::error_code compute_hi(
    EVP_MAC_CTX* ctx,
    std::span<const unsigned char> str,
    std::span<const unsigned char> salt,
    std::uint32_t iteration_count,
    sha256_digest& res
)
{
    // Hi(str, salt, i):
    //   U1   := HMAC(str, salt + INT(1))
    //   U2   := HMAC(str, U1)
    //   ...
    //   Ui-1 := HMAC(str, Ui-2)
    //   Ui   := HMAC(str, Ui-1)
    //   Hi := U1 XOR U2 XOR ... XOR Ui

    // INT(g) is a 4-octet encoding of the integer g, most significant octet first.
    unsigned char int1[4]{};
    boost::endian::store_big_s32(int1, static_cast<std::int32_t>(1));

    // Compute U1
    sha256_digest ucurrent{};
    if (auto ec = compute_hmac(ctx, str, salt, int1, ucurrent))
        return ec;

    // Compute the rest of the values
    sha256_digest hi = ucurrent;
    for (std::size_t i = 1u; i < iteration_count; ++i)
    {
        sha256_digest unext;
        if (auto ec = compute_hmac(ctx, str, ucurrent, unext))
            return ec;
        hi = compute_xor(hi, unext);
        ucurrent = unext;
    }

    // Done
    res = hi;
    return {};
}

// One-shot SHA256 calculation, but with error handling
[[nodiscard]] inline boost::system::error_code compute_sha256(
    std::span<const unsigned char> data,
    sha256_digest& output
)
{
    // Helpers
    struct md_deleter
    {
        void operator()(EVP_MD* p) const { EVP_MD_free(p); }
    };
    struct evp_ctx_deleter
    {
        void operator()(EVP_MD_CTX* p) const { EVP_MD_CTX_free(p); }
    };

    // Fetch the SHA256 message digest
    std::unique_ptr<EVP_MD, md_deleter> md(EVP_MD_fetch(nullptr, "SHA256", nullptr));
    if (!md)
        return ::nativepg::detail::translate_openssl_error(ERR_get_error());

    std::unique_ptr<EVP_MD_CTX, evp_ctx_deleter> ctx(EVP_MD_CTX_new());
    if (!ctx)
        return ::nativepg::detail::translate_openssl_error(ERR_get_error());

    // Actual computation
    unsigned int outlen = 0;
    if (!EVP_DigestInit_ex(ctx.get(), md.get(), nullptr) ||
        !EVP_DigestUpdate(ctx.get(), data.data(), data.size()) ||
        !EVP_DigestFinal_ex(ctx.get(), output.data(), &outlen))
    {
        return ::nativepg::detail::translate_openssl_error(ERR_get_error());
    }
    BOOST_ASSERT(outlen == output.size());
    return {};
}

// Performs the entire proof computation process
[[nodiscard]] inline boost::system::error_code compute_proofs(
    std::string_view password,
    std::span<const unsigned char> salt,
    std::uint32_t iteration_count,
    std::span<const unsigned char> auth_message,
    sha256_digest& client_proof,
    sha256_digest& server_signature
)
{
    // OpenSSL resource handling
    struct evp_mac_deleter
    {
        void operator()(EVP_MAC* p) const { EVP_MAC_free(p); }
    };

    struct evp_mac_ctx_deleter
    {
        void operator()(EVP_MAC_CTX* p) const { EVP_MAC_CTX_free(p); }
    };

    // Create a MAC ctx
    std::unique_ptr<EVP_MAC, evp_mac_deleter> mac{EVP_MAC_fetch(nullptr, "HMAC", nullptr)};
    if (!mac)
        return ::nativepg::detail::translate_openssl_error(ERR_get_error());

    std::unique_ptr<EVP_MAC_CTX, evp_mac_ctx_deleter> ctx{EVP_MAC_CTX_new(mac.get())};
    if (!ctx)
        return ::nativepg::detail::translate_openssl_error(ERR_get_error());

    char digest_name[] = "SHA256";
    OSSL_PARAM params[] = {
        OSSL_PARAM_construct_utf8_string(OSSL_MAC_PARAM_DIGEST, digest_name, 0),
        OSSL_PARAM_construct_end(),
    };
    if (!EVP_MAC_CTX_set_params(ctx.get(), params))
        return ::nativepg::detail::translate_openssl_error(ERR_get_error());

    // Normalize the password
    std::string normalized_password;
    normalize_password(password, normalized_password);

    //  SaltedPassword  := Hi(Normalize(password), salt, i)
    sha256_digest salted_password{};
    if (auto ec = compute_hi(ctx.get(), to_span(normalized_password), salt, iteration_count, salted_password))
        return ec;

    //  ClientKey       := HMAC(SaltedPassword, "Client Key")
    sha256_digest client_key{};
    if (auto ec = compute_hmac(ctx.get(), salted_password, to_span("Client Key"), client_key))
        return ec;

    //  StoredKey       := H(ClientKey)
    sha256_digest stored_key{};
    if (auto ec = compute_sha256(client_key, stored_key))
        return ec;

    //  ClientSignature := HMAC(StoredKey, AuthMessage)
    sha256_digest client_signature{};
    if (auto ec = compute_hmac(ctx.get(), stored_key, auth_message, client_key))
        return ec;

    //  ClientProof     := ClientKey XOR ClientSignature
    client_proof = compute_xor(client_key, client_signature);

    //  ServerKey       := HMAC(SaltedPassword, "Server Key")
    sha256_digest server_key{};
    if (auto ec = compute_hmac(ctx.get(), salted_password, to_span("Server Key"), server_key))
        return ec;

    //  ServerSignature := HMAC(ServerKey, AuthMessage)
    return compute_hmac(ctx.get(), server_key, auth_message, server_signature);
}

// TODO: this works but the std::string/std::vector interface is generating friction
[[nodiscard]] inline boost::system::error_code generate_nonce(std::string& to)
{
    // 18 bytes of random data, matching libpq's nonce length
    unsigned char raw[18];
    if (RAND_bytes(raw, static_cast<int>(sizeof(raw))) != 1)
        return ::nativepg::detail::translate_openssl_error(ERR_get_error());

    // Base64-encode into a temporary buffer
    std::vector<unsigned char> encoded;
    base64_encode(raw, encoded);

    // Hand the encoded characters back to the caller as a string
    to.assign(encoded.begin(), encoded.end());
    return {};
}

}  // namespace nativepg::protocol::detail::scram_sha256

#endif
