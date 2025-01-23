//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_PROTOCOL_SERIALIZATION_CONTEXT_HPP
#define NATIVEPG_PROTOCOL_SERIALIZATION_CONTEXT_HPP

#include <boost/core/span.hpp>
#include <boost/endian/conversion.hpp>

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace nativepg {
namespace detail {

class serialization_context
{
    std::vector<unsigned char>& buffer_;

public:
    serialization_context(std::vector<std::uint8_t>& buff) noexcept : buffer_(buff) {}

    template <class IntType>
    void add_integral(IntType value)
    {
        unsigned char buff[sizeof(IntType)];
        boost::endian::endian_store<IntType, sizeof(IntType), boost::endian::order::big>(buff, value);
        add_bytes(buff);
    }

    void add_string(std::string_view s)
    {
        add_bytes({reinterpret_cast<const unsigned char*>(s.data()), s.size()});
        add_byte(0);  // NULL terminator
    }

    void add_bytes(boost::span<const unsigned char> contents)
    {
        buffer_.insert(buffer_.end(), contents.begin(), contents.end());
    }

    void add_byte(unsigned char byte) { buffer_.push_back(byte); }

    template <class Message>
    void add_message(const Message& msg)
    {
        // Add the message type
        add_byte(Message::message_type);

        // Record initial size
        auto initial_offset = buffer_.size();

        // Add space for the message length
        add_bytes(std::array<unsigned char, 4>{});

        // Serialize the message
        msg.serialize(*this);

        // Compute the length. TODO: check length?
        auto total_length = static_cast<std::uint32_t>(buffer_.size() - initial_offset);

        // Serialize the length
        boost::endian::store_big_u32(buffer_.data() + initial_offset, total_length);
    }
};

}  // namespace detail
}  // namespace nativepg

#endif
