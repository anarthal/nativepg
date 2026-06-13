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

#include "nativepg/protocol/any_backend_message.hpp"

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
        any_backend_message message;

        // if !ec, byte offset where the message ends, to pass to consume()
        // if ec == client_errc::needs_more, minimum amount of buffer space required
        std::size_t size{};
    };

    result next();
};

// TODO: move
// Gets how many bytes we're missing to have a complete message in buff.
// This function should be called iteratively until it returns 0.
std::size_t message_missing_bytes(std::span<const unsigned char> buff);

}  // namespace nativepg::protocol

#endif
