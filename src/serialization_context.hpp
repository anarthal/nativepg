//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_SRC_SERIALIZATION_CONTEXT_HPP
#define NATIVEPG_SRC_SERIALIZATION_CONTEXT_HPP

#include <boost/assert.hpp>
#include <boost/core/span.hpp>
#include <boost/endian/conversion.hpp>
#include <boost/system/detail/error_code.hpp>
#include <boost/system/error_code.hpp>

#include <array>
#include <cstdint>
#include <limits>
#include <string_view>
#include <vector>

namespace nativepg {
namespace protocol {
namespace detail {

class serialization_context
{
    std::vector<unsigned char>& buffer_;
    boost::system::error_code err_;

public:
    serialization_context(std::vector<std::uint8_t>& buff) noexcept : buffer_(buff) {}

    std::vector<unsigned char>& buffer() { return buffer_; }

    void add_error(boost::system::error_code ec)
    {
        if (!err_)
            err_ = ec;
    }

    boost::system::error_code error() const { return err_; }

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

    void add_header(char msg_type)
    {
        // Add the message type
        add_byte(static_cast<unsigned char>(msg_type));

        // Add space for the message length
        add_bytes(std::array<unsigned char, 4>{});
    }

    // Assumes you have called add_header at the beginning
    boost::system::error_code finalize_message()
    {
        // Error check
        if (err_)
            return err_;

        // Compute the length to serialize. Everything except the type
        BOOST_ASSERT(buffer_.size() >= 5u);
        auto size = buffer_.size() - 1u;

        // Check that the length doesn't exceed an int32
        if (size > (std::numeric_limits<std::int32_t>::max)())
            return err_;

        // Serialize the message size
        boost::endian::store_big_s32(buffer_.data() + 1u, static_cast<std::int32_t>(size));

        // Done
        return {};
    }

    template <class Message>
    void add_message(const Message& msg)
    {
        // Add the message type
        add_byte(Message::message_type);

        // Record initial size
        auto initial_offset = buffer_.size();

        // Serialize the message
        msg.serialize(*this);

        // Compute the length. TODO: check length?
        auto total_length = static_cast<std::uint32_t>(buffer_.size() - initial_offset);

        // Serialize the length
        boost::endian::store_big_u32(buffer_.data() + initial_offset, total_length);
    }
};

}  // namespace detail
}  // namespace protocol
}  // namespace nativepg

#endif
