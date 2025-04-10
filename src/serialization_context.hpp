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
#include <cstddef>
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
    std::size_t header_offset_{static_cast<std::size_t>(-1)};  // where is the message header?
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

    void add_bytes(std::string_view contents)
    {
        add_bytes(boost::span<const unsigned char>(
            reinterpret_cast<const unsigned char*>(contents.data()),
            contents.size()
        ));
    }

    void add_byte(unsigned char byte) { buffer_.push_back(byte); }

    void add_header(char msg_type)
    {
        // There shouldn't be an in-progress message
        BOOST_ASSERT(header_offset_ == static_cast<std::size_t>(-1));

        // Record the header offset
        header_offset_ = buffer_.size();

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
        BOOST_ASSERT(buffer_.size() >= header_offset_ + 5u);
        auto size = buffer_.size() - header_offset_ - 1u;

        // Check that the length doesn't exceed an int32
        if (size > (std::numeric_limits<std::int32_t>::max)())
            return err_;

        // Serialize the message size
        boost::endian::store_big_s32(buffer_.data() + header_offset_ + 1u, static_cast<std::int32_t>(size));

        // Record that we're no longer composing a message
        header_offset_ = static_cast<std::size_t>(-1);

        // Done
        return {};
    }
};

}  // namespace detail
}  // namespace protocol
}  // namespace nativepg

#endif
