//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_PARSE_MESSAGE_HPP
#define NATIVEPG_PARSE_MESSAGE_HPP

#include <boost/system/error_code.hpp>

#include <cstddef>
#include <span>

#include "nativepg/protocol/any_backend_message.hpp"

// Functions to parse a stream of messages into any_backend_message

namespace nativepg::protocol {

struct parse_message_result
{
    // empty, needs_more or any other error
    boost::system::error_code ec;

    // if !ec
    any_backend_message message{};

    // if !ec, byte length of the message, to pass to consume()
    // if ec == client_errc::needs_more, minimum amount of buffer space required
    std::size_t size{};
};

parse_message_result parse_message(std::span<const unsigned char> data);

// Gets how many bytes we're missing to have a complete message in data.
// This function should be called iteratively until it returns 0.
std::size_t message_missing_bytes(std::span<const unsigned char> data);

}  // namespace nativepg::protocol

#endif
