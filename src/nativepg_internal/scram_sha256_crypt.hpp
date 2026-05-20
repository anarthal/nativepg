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
#include <boost/system/result.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <openssl/core_names.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/params.h>
#include <openssl/types.h>
#include <span>
#include <string>
#include <string_view>

#include "nativepg_internal/openssl_error.hpp"

// Functions to compute the values used by SCRAM-SHA256

namespace nativepg::protocol::scram_sha256 {

using sha256_digest = std::array<unsigned char, 32u>;

// OpenSSL resource handling
struct evp_mac_deleter
{
    void operator()(EVP_MAC* p) const { EVP_MAC_free(p); }
};
using unique_evp_mac = std::unique_ptr<EVP_MAC, evp_mac_deleter>;

struct evp_mac_ctx_deleter
{
    void operator()(EVP_MAC_CTX* p) const { EVP_MAC_CTX_free(p); }
};
using unique_evp_mac_ctx = std::unique_ptr<EVP_MAC_CTX, evp_mac_ctx_deleter>;

// Tries to apply the StringPrep algorithm with the SASLPrep profile to input.
// TODO: this is a complex algorithm and is not implemented yet.
// It should be valid for ASCII passwords, at least
[[nodiscard]] inline boost::system::error_code sasl_prep(std::string_view input, std::string& output)
{
    output = input;
    return {};
}

// Prepares a password by either applying sasl_prep, or leaving it as-is
inline void normalize_password(std::string_view input, std::string& output)
{
    // Try to do SASLPrep.
    // If that fails, use the password as-is (as per the postgres spec)
    if (auto ec = sasl_prep(input, output))
        output = input;
}

//  SaltedPassword  := Hi(Normalize(password), salt, i)
[[nodiscard]] inline boost::system::result<sha256_digest> salt_password(
    std::string_view normalized_password,
    std::span<const unsigned char> salt,
    std::uint32_t iteration_count
)
{
    // Fetch the PBKDF2 KDF
    std::unique_ptr<EVP_KDF, decltype(&EVP_KDF_free)> kdf(
        EVP_KDF_fetch(nullptr, "PBKDF2", nullptr),
        &EVP_KDF_free
    );
    if (!kdf)
        return ::nativepg::detail::translate_openssl_error(ERR_get_error());

    std::unique_ptr<EVP_KDF_CTX, decltype(&EVP_KDF_CTX_free)> ctx(
        EVP_KDF_CTX_new(kdf.get()),
        &EVP_KDF_CTX_free
    );
    if (!ctx)
        return ::nativepg::detail::translate_openssl_error(ERR_get_error());

    char digest_name[] = "SHA256";
    OSSL_PARAM params[5];
    params[0] = OSSL_PARAM_construct_octet_string(
        OSSL_KDF_PARAM_PASSWORD,
        const_cast<char*>(normalized_password.data()),
        normalized_password.size()
    );
    params[1] = OSSL_PARAM_construct_octet_string(
        OSSL_KDF_PARAM_SALT,
        const_cast<unsigned char*>(salt.data()),
        salt.size()
    );
    params[2] = OSSL_PARAM_construct_uint32(OSSL_KDF_PARAM_ITER, &iteration_count);
    params[3] = OSSL_PARAM_construct_utf8_string(OSSL_KDF_PARAM_DIGEST, digest_name, 0);
    params[4] = OSSL_PARAM_construct_end();

    sha256_digest result{};
    if (EVP_KDF_derive(ctx.get(), result.data(), result.size(), params) <= 0)
        return ::nativepg::detail::translate_openssl_error(ERR_get_error());

    return result;
}

//  ClientKey       := HMAC(SaltedPassword, "Client Key")
[[nodiscard]] inline boost::system::result<sha256_digest> compute_client_key(
    EVP_MAC_CTX* ctx,
    const sha256_digest& salted_password
)
{
    constexpr std::string_view msg_str = "Client Key";

    // (Re)initialize the context
    if (!EVP_MAC_init(ctx, salted_password.data(), salted_password.size(), nullptr))
        return ::nativepg::detail::translate_openssl_error(ERR_get_error());

    // Supply data
    if (!EVP_MAC_update(ctx, reinterpret_cast<const unsigned char*>(msg_str.data()), msg_str.size()))
        return ::nativepg::detail::translate_openssl_error(ERR_get_error());

    // Finalize. This digest is fixed size
    sha256_digest result{};
    std::size_t outlen = 0;
    if (!EVP_MAC_final(ctx, result.data(), &outlen, result.size()))
        return ::nativepg::detail::translate_openssl_error(ERR_get_error());
    BOOST_ASSERT(outlen == result.size());

    return result;
}

//  StoredKey       := H(ClientKey)
[[nodiscard]] inline boost::system::result<sha256_digest> compute_stored_key(const sha256_digest& client_key)
{
    // Fetch the SHA256 message digest
    std::unique_ptr<EVP_MD, decltype(&EVP_MD_free)> md(
        EVP_MD_fetch(nullptr, "SHA256", nullptr),
        &EVP_MD_free
    );
    if (!md)
        return ::nativepg::detail::translate_openssl_error(ERR_get_error());

    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx(EVP_MD_CTX_new(), &EVP_MD_CTX_free);
    if (!ctx)
        return ::nativepg::detail::translate_openssl_error(ERR_get_error());

    if (!EVP_DigestInit_ex(ctx.get(), md.get(), nullptr))
        return ::nativepg::detail::translate_openssl_error(ERR_get_error());

    if (!EVP_DigestUpdate(ctx.get(), client_key.data(), client_key.size()))
        return ::nativepg::detail::translate_openssl_error(ERR_get_error());

    sha256_digest result{};
    unsigned int outlen = 0;
    if (!EVP_DigestFinal_ex(ctx.get(), result.data(), &outlen))
        return ::nativepg::detail::translate_openssl_error(ERR_get_error());
    BOOST_ASSERT(outlen == result.size());

    return result;
}

//  ClientSignature := HMAC(StoredKey, AuthMessage)
[[nodiscard]] inline boost::system::result<sha256_digest> compute_client_signature(
    const sha256_digest& stored_key,
    std::span<const unsigned char> auth_msg
)
{
    // Fetch the HMAC algorithm
    std::unique_ptr<EVP_MAC, decltype(&EVP_MAC_free)> mac(
        EVP_MAC_fetch(nullptr, "HMAC", nullptr),
        &EVP_MAC_free
    );
    if (!mac)
        return ::nativepg::detail::translate_openssl_error(ERR_get_error());

    std::unique_ptr<EVP_MAC_CTX, decltype(&EVP_MAC_CTX_free)> ctx(
        EVP_MAC_CTX_new(mac.get()),
        &EVP_MAC_CTX_free
    );
    if (!ctx)
        return ::nativepg::detail::translate_openssl_error(ERR_get_error());

    OSSL_PARAM params[2];
    char digest_name[] = "SHA256";
    params[0] = OSSL_PARAM_construct_utf8_string(OSSL_MAC_PARAM_DIGEST, digest_name, 0);
    params[1] = OSSL_PARAM_construct_end();

    if (!EVP_MAC_init(ctx.get(), stored_key.data(), stored_key.size(), params))
        return ::nativepg::detail::translate_openssl_error(ERR_get_error());

    if (!EVP_MAC_update(ctx.get(), auth_msg.data(), auth_msg.size()))
        return ::nativepg::detail::translate_openssl_error(ERR_get_error());

    sha256_digest result{};
    std::size_t outlen = 0;
    if (!EVP_MAC_final(ctx.get(), result.data(), &outlen, result.size()))
        return ::nativepg::detail::translate_openssl_error(ERR_get_error());
    BOOST_ASSERT(outlen == result.size());

    return result;
}

//  ClientProof     := ClientKey XOR ClientSignature
[[nodiscard]] inline sha256_digest compute_client_proof(
    const sha256_digest& client_key,
    const sha256_digest& client_signature
)
{
    sha256_digest result{};
    for (std::size_t i = 0; i < result.size(); ++i)
        result[i] = client_key[i] ^ client_signature[i];
    return result;
}

//  ServerKey       := HMAC(SaltedPassword, "Server Key")
[[nodiscard]] inline boost::system::result<sha256_digest> compute_server_key(
    const sha256_digest& salted_password
)
{
    constexpr std::string_view msg_str = "Server Key";

    // Fetch the HMAC algorithm
    std::unique_ptr<EVP_MAC, decltype(&EVP_MAC_free)> mac(
        EVP_MAC_fetch(nullptr, "HMAC", nullptr),
        &EVP_MAC_free
    );
    if (!mac)
        return ::nativepg::detail::translate_openssl_error(ERR_get_error());

    std::unique_ptr<EVP_MAC_CTX, decltype(&EVP_MAC_CTX_free)> ctx(
        EVP_MAC_CTX_new(mac.get()),
        &EVP_MAC_CTX_free
    );
    if (!ctx)
        return ::nativepg::detail::translate_openssl_error(ERR_get_error());

    OSSL_PARAM params[2];
    char digest_name[] = "SHA256";
    params[0] = OSSL_PARAM_construct_utf8_string(OSSL_MAC_PARAM_DIGEST, digest_name, 0);
    params[1] = OSSL_PARAM_construct_end();

    if (!EVP_MAC_init(ctx.get(), salted_password.data(), salted_password.size(), params))
        return ::nativepg::detail::translate_openssl_error(ERR_get_error());

    if (!EVP_MAC_update(ctx.get(), reinterpret_cast<const unsigned char*>(msg_str.data()), msg_str.size()))
        return ::nativepg::detail::translate_openssl_error(ERR_get_error());

    sha256_digest result{};
    std::size_t outlen = 0;
    if (!EVP_MAC_final(ctx.get(), result.data(), &outlen, result.size()))
        return ::nativepg::detail::translate_openssl_error(ERR_get_error());
    BOOST_ASSERT(outlen == result.size());

    return result;
}

//  ServerSignature := HMAC(ServerKey, AuthMessage)
[[nodiscard]] inline boost::system::result<sha256_digest> compute_server_signature(
    const sha256_digest& server_key,
    std::span<const unsigned char> auth_msg
)
{
    // Fetch the HMAC algorithm
    std::unique_ptr<EVP_MAC, decltype(&EVP_MAC_free)> mac(
        EVP_MAC_fetch(nullptr, "HMAC", nullptr),
        &EVP_MAC_free
    );
    if (!mac)
        return ::nativepg::detail::translate_openssl_error(ERR_get_error());

    std::unique_ptr<EVP_MAC_CTX, decltype(&EVP_MAC_CTX_free)> ctx(
        EVP_MAC_CTX_new(mac.get()),
        &EVP_MAC_CTX_free
    );
    if (!ctx)
        return ::nativepg::detail::translate_openssl_error(ERR_get_error());

    OSSL_PARAM params[2];
    char digest_name[] = "SHA256";
    params[0] = OSSL_PARAM_construct_utf8_string(OSSL_MAC_PARAM_DIGEST, digest_name, 0);
    params[1] = OSSL_PARAM_construct_end();

    if (!EVP_MAC_init(ctx.get(), server_key.data(), server_key.size(), params))
        return ::nativepg::detail::translate_openssl_error(ERR_get_error());

    if (!EVP_MAC_update(ctx.get(), auth_msg.data(), auth_msg.size()))
        return ::nativepg::detail::translate_openssl_error(ERR_get_error());

    sha256_digest result{};
    std::size_t outlen = 0;
    if (!EVP_MAC_final(ctx.get(), result.data(), &outlen, result.size()))
        return ::nativepg::detail::translate_openssl_error(ERR_get_error());
    BOOST_ASSERT(outlen == result.size());

    return result;
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
    // Create a MAC ctx
    unique_evp_mac mac{EVP_MAC_fetch(nullptr, "HMAC", nullptr)};
    if (!mac)
        return ::nativepg::detail::translate_openssl_error(ERR_get_error());

    unique_evp_mac_ctx ctx{EVP_MAC_CTX_new(mac.get())};
    if (!ctx)
        return ::nativepg::detail::translate_openssl_error(ERR_get_error());

    char digest_name[] = "SHA256";
    OSSL_PARAM params[] = {
        OSSL_PARAM_construct_utf8_string(OSSL_MAC_PARAM_DIGEST, digest_name, 0),
        OSSL_PARAM_construct_end(),
    };
    if (!EVP_MAC_CTX_set_params(ctx.get(), params))
        return ::nativepg::detail::translate_openssl_error(ERR_get_error());

    //  SaltedPassword  := Hi(Normalize(password), salt, i)
    std::string normal_pass;
    normalize_password(password, normal_pass);
    auto salted_password_res = salt_password(normal_pass, salt, iteration_count);
    if (salted_password_res.has_error())
        return salted_password_res.error();
    const auto& salted_password = *salted_password_res;

    //  ClientKey       := HMAC(SaltedPassword, "Client Key")
    auto client_key_res = compute_client_key(ctx.get(), salted_password);
    if (client_key_res.has_error())
        return client_key_res.error();
    const auto& client_key = *client_key_res;

    //  StoredKey       := H(ClientKey)
    auto stored_key_res = compute_stored_key(client_key);
    if (stored_key_res.has_error())
        return stored_key_res.error();
    const auto& stored_key = *stored_key_res;

    //  ClientSignature := HMAC(StoredKey, AuthMessage)
    auto client_signature_res = compute_client_signature(stored_key, auth_message);
    if (client_signature_res.has_error())
        return client_signature_res.error();
    auto& client_signature = *client_signature_res;

    //  ClientProof     := ClientKey XOR ClientSignature
    client_proof = compute_client_proof(client_key, client_signature);

    //  ServerKey       := HMAC(SaltedPassword, "Server Key")
    auto server_key_res = compute_server_key(salted_password);
    if (server_key_res.has_error())
        return server_key_res.error();
    const auto& server_key = *server_key_res;

    //  ServerSignature := HMAC(ServerKey, AuthMessage)
    auto server_signature_res = compute_server_signature(server_key, auth_message);
    if (server_signature_res.has_error())
        return server_signature_res.error();
    server_signature = *server_signature_res;

    return {};
}

// Nonces are 18 bytes of binary output, then base64 encoded
[[nodiscard]] boost::system::error_code generate_nonce(std::string& to);

}  // namespace nativepg::protocol::scram_sha256

#endif
