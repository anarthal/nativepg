//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_PROTOCOL_SCRAM_SHA256_FSM_HPP
#define NATIVEPG_PROTOCOL_SCRAM_SHA256_FSM_HPP

#include <boost/system/error_code.hpp>

#include <array>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace nativepg::protocol::detail {

class scram_sha256_fsm
{
    std::string nonce_;
    std::span<const unsigned char> client_first_msg_;
    std::array<unsigned char, 32u> server_signature_{};

public:
    scram_sha256_fsm() = default;

    // Composes the client initial message. The nonce generator is passed as parameter for testability
    using nonce_generator = boost::system::error_code (*)(std::string&);
    boost::system::error_code on_init(nonce_generator nonce_gen, std::vector<unsigned char>& write_buffer);
    boost::system::error_code on_init(std::vector<unsigned char>& write_buffer);

    // Checks the server nonce, computes proofs and composes the client final message
    boost::system::error_code on_server_first(
        std::span<const unsigned char> msg,
        std::string_view password,
        std::vector<unsigned char>& write_buffer
    );

    // Checks the server proof
    boost::system::error_code on_server_final(std::span<const unsigned char> msg);
};

}  // namespace nativepg::protocol::detail

#endif
