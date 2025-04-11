//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_PROTOCOL_SCRAM_SHA256_HPP
#define NATIVEPG_PROTOCOL_SCRAM_SHA256_HPP

#include <boost/core/span.hpp>
#include <boost/system/error_code.hpp>
#include <boost/system/result.hpp>

#include <cstdint>
#include <string_view>
#include <vector>

namespace nativepg {
namespace protocol {

// This is really a password message. serialize serializes the entire message,
// including the header. Returns the client-first-message-bare part of the serialized
// message, required by the SCRAM algorithm
struct scram_sha256_client_first_message
{
    // The SASL mechanism name that was chosen
    std::string_view mechanism;

    // The nonce sent to the server
    std::string_view nonce;
};
boost::system::result<boost::span<const unsigned char>> serialize(
    const scram_sha256_client_first_message& msg,
    std::vector<unsigned char>& to
);

// This is an authentication_sasl_continue message. parse does not parse the type
struct scram_sha256_server_first_message
{
    // The nonce sent by the server, should contain the nonce we sent and an extra value created by the server
    std::string_view nonce;

    // The salt sent by the server. This comes as base64
    // TODO: salt is likely restricted in size, so we could use a static_vector and make this non-allocating
    std::vector<unsigned char> salt;

    // Iteration count. As an arbitrary fixed-width integer to set an upper limit
    std::uint32_t iteration_count;
};
boost::system::error_code parse(boost::span<const unsigned char> data, scram_sha256_server_first_message&);

// This is an authentication_sasl_final
struct scram_sha256_client_final_message
{
    // The nonce sent to the server
    std::string_view nonce;

    // Proof that we have the password, to be b64 encoded
    boost::span<const unsigned char> proof;
};
boost::system::error_code serialize(
    const scram_sha256_client_final_message& msg,
    std::vector<unsigned char>& to
);

}  // namespace protocol
}  // namespace nativepg

#endif
