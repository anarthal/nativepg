//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_PROTOCOL_HEADER_HPP
#define NATIVEPG_PROTOCOL_HEADER_HPP

#include <boost/core/span.hpp>
#include <boost/system/result.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

namespace nativepg {
namespace protocol {

// Message header operations
struct message_header
{
    std::uint8_t type;  // The message type
    std::size_t size;   // Should be < INT32_MAX
};

boost::system::result<message_header> parse_header(boost::span<const unsigned char, 5> from);

// Might fail if length is too big
boost::system::result<std::array<unsigned char, 5>> serialize_header(message_header header);

}  // namespace protocol
}  // namespace nativepg

#endif
