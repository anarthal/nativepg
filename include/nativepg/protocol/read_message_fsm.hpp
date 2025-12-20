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

#include "nativepg/protocol/messages.hpp"
#include "nativepg/protocol/views.hpp"

namespace nativepg::protocol {

struct connection_state;

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
            : type_(result_type::message), msg_{msg, bytes_consumed}
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
            return msg_.msg;
        }

        std::size_t bytes_consumed() const
        {
            BOOST_ASSERT(type_ == result_type::message);
            return msg_.bytes_consumed;
        }

    private:
        result_type type_;
        struct msg_t
        {
            any_backend_message msg;
            std::size_t bytes_consumed;
        };

        union
        {
            boost::system::error_code ec_;
            msg_t msg_;
            std::size_t hint_;
        };
    };

    read_message_fsm() = default;

    result resume(std::span<const unsigned char> data);
};

// This is like read_message_fsm, but has knowledge of connection_state buffers
// and remembers the bytes consumed from message to message.
// TODO: unit tests
class read_message_stream_fsm
{
public:
    enum class result_type
    {
        read,
        error,
        message,
    };

    class result
    {
    public:
        result(boost::system::error_code ec) noexcept : type_(result_type::error), ec_(ec) {}
        result(std::span<unsigned char> read_buff) noexcept : type_(result_type::read), read_buff_(read_buff)
        {
        }
        result(const any_backend_message& msg) noexcept : type_(result_type::message), msg_{msg} {}

        result_type type() const { return type_; }

        boost::system::error_code error() const
        {
            BOOST_ASSERT(type_ == result_type::error);
            return ec_;
        }

        std::span<unsigned char> read_buffer() const
        {
            BOOST_ASSERT(type_ == result_type::read);
            return read_buff_;
        }

        const any_backend_message& message() const
        {
            BOOST_ASSERT(type_ == result_type::message);
            return msg_;
        }

    private:
        result_type type_;

        union
        {
            boost::system::error_code ec_;
            any_backend_message msg_;
            std::span<unsigned char> read_buff_;
        };
    };

    read_message_stream_fsm() = default;

    result resume(connection_state& st, boost::system::error_code io_ec, std::size_t bytes_read);

private:
    int resume_point_{0};
    std::size_t bytes_to_consume_{};
    read_message_fsm fsm_;
};

}  // namespace nativepg::protocol

#endif
