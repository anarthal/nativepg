//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_PROTOCOL_PARSE_MESSAGE_FSM_HPP
#define NATIVEPG_PROTOCOL_PARSE_MESSAGE_FSM_HPP

#include <boost/system/error_code.hpp>

#include <cstddef>
#include <cstdint>
#include <span>

#include "nativepg/protocol/header.hpp"
#include "nativepg/protocol/messages.hpp"
#include "nativepg/protocol/views.hpp"

namespace nativepg::protocol {

class parse_message_fsm
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
        result(const any_backend_message& msg) noexcept : type_(result_type::message), msg_(msg) {}

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
            return msg_;
        }

    private:
        result_type type_;
        union
        {
            boost::system::error_code ec_;
            any_backend_message msg_;
            std::size_t hint_;
        };
    };

    parse_message_fsm() = default;

    result resume(std::span<const unsigned char> data)
    {
        if (msg_size_ == -1)
        {
            // Header. Ensure we have enough data
            if (data.size() < 5u)
                return result(5u);

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
            return result(expected_size);

        // Do the parsing
        auto msg_result = parse(msg_type_, data.subspan(0, expected_size));
        if (msg_result.has_error())
            return msg_result.error();
        return *msg_result;
    }
};

}  // namespace nativepg::protocol

#endif
