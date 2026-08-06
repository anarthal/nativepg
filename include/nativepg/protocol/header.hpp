//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_PROTOCOL_HEADER_HPP
#define NATIVEPG_PROTOCOL_HEADER_HPP

#include <cstdint>
#include <span>
#include <system_error>

namespace nativepg {
namespace protocol {

// Message header operations
struct message_header
{
    std::uint8_t type;  // The message type
    std::int32_t size;  // Will always be >= 4
};

std::error_code parse_header(std::span<const unsigned char, 5> from, message_header& to);

// Might fail if length is too big
std::error_code serialize_header(const message_header& header, std::span<unsigned char, 5u> to);

}  // namespace protocol
}  // namespace nativepg

#endif
