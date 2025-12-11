//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_PROTOCOL_READ_MESSAGE_FSM_HPP
#define NATIVEPG_PROTOCOL_READ_MESSAGE_FSM_HPP

#include <boost/system/error_code.hpp>

#include <cstddef>
#include <cstdint>
#include <span>

#include "nativepg/protocol/header.hpp"
#include "nativepg/protocol/messages.hpp"
#include "nativepg/protocol/views.hpp"

namespace nativepg::protocol {

// A finite-state machine type to read messages from the server.
// resume() returns a variant-like type specifying what to do next.
// Flow should be:
//   - Create a new FSM per message (they're lightweight)
//   - Call resume() passing all the bytes available in your read buffer
//   - If resume returns an error, a serious protocol violation happened. Not recoverable.
//   - If resume returns needs_more, we need to read more data from the server.
//     At least result::hint() bytes should be read. Read and resume again with your entire buffer.
//   - If resume() returns a message, a message is available. Use the message and then call consume
//     with result::bytes_consumed(). Remember that messages point into the network buffer, so
//     don't call consume before using the message.
class read_message_fsm
{
    std::uint8_t msg_type_{};
    std::int32_t msg_size_{-1};

public:
    enum class result_type
    {
        needs_more,
        error,
        message,
    };

    class result
    {
    public:
        result(boost::system::error_code ec) noexcept : type_(result_type::error), ec_(ec) {}
        result(std::size_t hint) noexcept : type_(result_type::needs_more), hint_(hint) {}
        result(const any_backend_message& msg, std::size_t bytes_consumed) noexcept
            : type_(result_type::message), msg_(msg, bytes_consumed)
        {
        }

        result_type type() const { return type_; }

        boost::system::error_code error() const
        {
            BOOST_ASSERT(type_ == result_type::error);
            return ec_;
        }

        std::size_t hint() const
        {
            BOOST_ASSERT(type_ == result_type::needs_more);
            return hint_;
        }

        const any_backend_message& message() const
        {
            BOOST_ASSERT(type_ == result_type::message);
            return msg_.first;
        }

        std::size_t bytes_consumed() const
        {
            BOOST_ASSERT(type_ == result_type::message);
            return msg_.second;
        }

    private:
        result_type type_;
        union
        {
            boost::system::error_code ec_;
            std::pair<any_backend_message, std::size_t> msg_;
            std::size_t hint_;
        };
    };

    read_message_fsm() = default;

    result resume(std::span<const unsigned char> data)
    {
        if (msg_size_ == -1)
        {
            // Header. Ensure we have enough data
            if (data.size() < 5u)
                return result(static_cast<std::size_t>(5u - data.size()));

            // Do the parsing
            auto header_result = parse_header(boost::span<const unsigned char, 5>(data));
            if (header_result.has_error())
                return header_result.error();

            // Record the header fields to signal that we're done with the header
            auto header = *header_result;
            msg_type_ = header.type;
            msg_size_ = header.size;
        }

        // Body. Ensure we have enough data. The header is not discarded
        // until the message is fully parsed for simplicity,
        // and the type byte is not included in the length
        BOOST_ASSERT(msg_size_ != -1);
        const auto expected_size = static_cast<std::size_t>(msg_size_ + 1);
        if (data.size() < expected_size)
            return result(static_cast<std::size_t>(expected_size - data.size()));

        // Do the parsing
        auto msg_result = parse(msg_type_, data.subspan(5, expected_size - 5));
        if (msg_result.has_error())
            return msg_result.error();
        return result(*msg_result, expected_size);
    }
};

}  // namespace nativepg::protocol

#endif
