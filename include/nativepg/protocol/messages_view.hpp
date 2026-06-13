//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_MESSAGES_VIEW_HPP
#define NATIVEPG_MESSAGES_VIEW_HPP

#include <boost/system/error_code.hpp>

#include <cstddef>
#include <span>

#include "nativepg/client_errc.hpp"
#include "nativepg/protocol/any_backend_message.hpp"
#include "nativepg/protocol/header.hpp"

namespace nativepg::protocol {

class messages_view
{
    std::span<const unsigned char> data_;

public:
    explicit messages_view(std::span<const unsigned char> data) noexcept : data_(data) {}

    struct result
    {
        // empty, needs_more or any other error
        boost::system::error_code ec;

        // if !ec
        any_backend_message message{};

        // if !ec, byte length of the message, to pass to consume()
        // if ec == client_errc::needs_more, minimum amount of buffer space required
        std::size_t size{};
    };

    result next()
    {
        // See if we have space for the header
        if (data_.size() < 5u)
            return {client_errc::needs_more, {}, 5u - data_.size()};

        // Load the header
        auto header_res = parse_header(boost::span<const unsigned char, 5>(data_));
        if (header_res.has_error())
            return {header_res.error()};

        // See if we have space for the body
        // The length in the header is the entire message's length, counting
        // the header length but not the type byte
        const auto required_size = static_cast<std::size_t>(header_res->size + 1u);
        if (data_.size() < required_size)
            return {client_errc::needs_more, {}, required_size - data_.size()};

        // Parse the body
        auto msg_result = parse(header_res->type, data_.subspan(5u, required_size - 5u));
        if (msg_result.has_error())
            return {msg_result.error()};

        // Advance
        data_ = data_.subspan(required_size);

        // Done
        return {{}, *msg_result, required_size};
    }
};

// TODO: move
// Gets how many bytes we're missing to have a complete message in data.
// This function should be called iteratively until it returns 0.
inline std::size_t message_missing_bytes(std::span<const unsigned char> data)
{
    // See if we have space for the header
    if (data.size() < 5u)
        return 5u - data.size();

    // Load the header
    auto header_res = parse_header(boost::span<const unsigned char, 5>(data));
    if (header_res.has_error())
        return 0u;  // Signal errors as complete. Message parsing will find them out

    // See if we have space for the body
    // The length in the header is the entire message's length, counting
    // the header length but not the type byte
    const auto required_size = static_cast<std::size_t>(header_res->size + 1u);
    if (data.size() < required_size)
        return required_size - data.size();

    // We have enough data
    return 0u;
}

}  // namespace nativepg::protocol

#endif
