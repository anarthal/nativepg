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
#include <string_view>
#include <vector>

#include "nativepg/client_errc.hpp"
#include "nativepg/protocol/detail/scram_sha256_fsm.hpp"
#include "nativepg_internal/scram_sha256_crypt.hpp"
#include "nativepg_internal/scram_sha256_messages.hpp"

using boost::system::error_code;
using namespace nativepg::protocol::detail::scram_sha256;

namespace nativepg::protocol::detail {

boost::system::error_code scram_sha256_fsm::on_init(
    nonce_generator nonce_gen,
    std::vector<unsigned char>& write_buffer
)
{
    // Compose the client initial message
    if (auto ec = nonce_gen(nonce_))
        return ec;

    // Serialize it
    write_buffer.clear();
    auto res = serialize(client_first_message{.mechanism = "SCRAM-SHA-256", .nonce = nonce_}, write_buffer);
    if (res.has_error())
        return res.error();

    // Save the initial message. Required to compute the client proof
    client_first_msg_ = *res;

    return {};
}

boost::system::error_code scram_sha256_fsm::on_init(std::vector<unsigned char>& write_buffer)
{
    return on_init(&generate_nonce, write_buffer);
}

boost::system::error_code scram_sha256_fsm::on_server_first(
    std::span<const unsigned char> bytes,
    std::string_view password,
    std::vector<unsigned char>& write_buffer
)
{
    // Parse the message
    server_first_message server_msg{};
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
    write_buffer.clear();
    client_final_message_serializer serializer{write_buffer};
    auto res = serializer.serialize_without_proof(server_msg.nonce);
    if (res.has_error())
        return res.error();

    //  AuthMessage     := client-first-message-bare + "," +
    //                     server-first-message + "," +
    //                     client-final-message-without-proof
    // TODO: maybe we could use views for some of these?
    auth_message.push_back(static_cast<unsigned char>(','));
    auth_message.insert(auth_message.end(), bytes.begin(), bytes.end());
    auth_message.push_back(static_cast<unsigned char>(','));
    auth_message.insert(auth_message.end(), res->begin(), res->end());

    // Compute the client proof and server signature
    sha256_digest client_proof;
    if (auto ec = compute_proofs(
            password,
            server_msg.salt,
            server_msg.iteration_count,
            auth_message,
            client_proof,
            server_signature_
        ))
    {
        return ec;
    }

    // Serialize the proof for the server
    return serializer.serialize_proof(client_proof);
}

boost::system::error_code scram_sha256_fsm::on_server_final(std::span<const unsigned char> bytes)
{
    // Parse the message
    server_final_message msg{};
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
