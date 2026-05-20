//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/system/error_code.hpp>

#include <algorithm>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "nativepg/client_errc.hpp"
#include "nativepg/protocol/detail/scram_sha256_fsm.hpp"
#include "nativepg/protocol/scram_sha256.hpp"
#include "nativepg_internal/scram_sha256_crypt.hpp"

using boost::system::error_code;
using namespace nativepg::protocol::scram_sha256;

namespace nativepg::protocol::detail {

boost::system::error_code scram_sha256_fsm::on_init(std::vector<unsigned char>& write_buffer)
{
    // Compose the client initial message
    if (auto ec = generate_nonce(nonce_))
        return ec;

    // Serialize it
    write_buffer.clear();
    auto res = serialize(
        scram_sha256_client_first_message{.mechanism = "SCRAM-SHA-256", .nonce = nonce_},
        write_buffer
    );
    if (res.has_error())
        return res.error();

    // Save the initial message. Required to compute the client proof
    client_first_msg_ = *res;

    return {};
}

boost::system::error_code scram_sha256_fsm::on_server_first(
    std::span<const unsigned char> bytes,
    std::string_view password,
    std::vector<unsigned char>& write_buffer
)
{
    // Parse the message
    scram_sha256_server_first_message server_msg{};
    if (auto ec = parse(bytes, server_msg))
        return ec;

    // Check the nonce. If it does start with our nonce, that's an error
    // TODO: we should also check the length here
    if (!server_msg.nonce.starts_with(nonce_))
        return error_code(client_errc::scram_invalid_nonce);

    // Reject too big iteration counts - this looks like a DDOS
    // TODO: do we need similar measures for salt?
    if (server_msg.iteration_count > 0xffff)
        return error_code(client_errc::protocol_value_error);

    // Save client-first-message-bare, as it is part of AuthMessage
    // and we need to use the write buffer, too
    std::vector<unsigned char> auth_message{client_first_msg_.begin(), client_first_msg_.end()};

    // Serialize the initial part of the client final message, as this is part of AuthMessage
    scram_sha256_client_final_message_serializer serializer{write_buffer};
    auto res = serializer.serialize_without_proof(server_msg.nonce);
    if (res.has_error())
        return res.error();

    //  SaltedPassword  := Hi(Normalize(password), salt, i)
    std::string normal_pass;
    normalize_password(password, normal_pass);
    auto salted_password_res = salt_password(
        normal_pass,
        server_msg.salt,
        server_msg.iteration_count
    );
    if (salted_password_res.has_error())
        return salted_password_res.error();
    auto& salted_password = *salted_password_res;

    //  ClientKey       := HMAC(SaltedPassword, "Client Key")
    auto client_key_res = compute_client_key(salted_password);
    if (client_key_res.has_error())
        return client_key_res.error();
    auto& client_key = *client_key_res;

    //  StoredKey       := H(ClientKey)
    auto stored_key_res = compute_stored_key(client_key);
    if (stored_key_res.has_error())
        return stored_key_res.error();
    auto& stored_key = *stored_key_res;

    //  AuthMessage     := client-first-message-bare + "," +
    //                     server-first-message + "," +
    //                     client-final-message-without-proof
    // TODO: maybe we could use views for some of these?
    auth_message.push_back(static_cast<unsigned char>(','));
    auth_message.insert(auth_message.end(), bytes.begin(), bytes.end());
    auth_message.push_back(static_cast<unsigned char>(','));
    auth_message.insert(auth_message.end(), res->begin(), res->end());

    //  ClientSignature := HMAC(StoredKey, AuthMessage)
    auto client_signature_res = compute_client_signature(stored_key, auth_message);
    if (client_signature_res.has_error())
        return client_signature_res.error();
    auto& client_signature = *client_signature_res;

    //  ClientProof     := ClientKey XOR ClientSignature
    auto client_proof = compute_client_proof(client_key, client_signature);

    //  ServerKey       := HMAC(SaltedPassword, "Server Key")
    auto server_key_res = compute_server_key(salted_password);
    if (server_key_res.has_error())
        return server_key_res.error();
    auto& server_key = *server_key_res;

    //  ServerSignature := HMAC(ServerKey, AuthMessage)
    // Store it for later validation
    auto server_signature_res = compute_server_signature(server_key, auth_message);
    if (server_signature_res.has_error())
        return server_signature_res.error();
    server_signature_ = *server_signature_res;

    // Serialize the proof for the server
    return serializer.serialize_proof(client_proof);
}

boost::system::error_code scram_sha256_fsm::on_server_final(std::span<const unsigned char> bytes)
{
    // Parse the message
    scram_sha256_server_final_message msg{};
    if (auto ec = parse(bytes, msg))
        return ec;

    // Check that the server signature is the same as the client's.
    // This is not a password, so timing attacks shouldn't be possible
    if (!std::ranges::equal(msg.server_signature, server_signature_))
        return client_errc::scram_invalid_server_signature;

    // Done
    return {};
}

}  // namespace nativepg::protocol::detail
