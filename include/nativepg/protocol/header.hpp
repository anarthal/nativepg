//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_PROTOCOL_HEADER_HPP
#define NATIVEPG_PROTOCOL_HEADER_HPP

#include <boost/core/span.hpp>

#include <cstdint>

namespace nativepg {
namespace protocol {

// Message header operations
struct message_header
{
    std::uint8_t type;  // The message type
    std::int32_t size;  // Should be >= 0
};
void serialize_header(message_header header, boost::span<unsigned char, 5> dest);
message_header parse_header(boost::span<const unsigned char, 5> from);

}  // namespace protocol
}  // namespace nativepg

#endif
