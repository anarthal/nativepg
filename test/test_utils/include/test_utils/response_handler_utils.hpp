//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_TEST_RESPONSE_HANDLER_UTILS_HPP
#define NATIVEPG_TEST_RESPONSE_HANDLER_UTILS_HPP

#include <cstddef>
#include <iosfwd>

#include "nativepg/responses/any_request_message.hpp"
#include "nativepg/responses/response_handler.hpp"

namespace nativepg::test {

struct on_msg_args
{
    any_request_message::kind type;
    std::size_t offset;

    friend bool operator==(const on_msg_args&, const on_msg_args&) = default;
};

std::ostream& operator<<(std::ostream& os, const on_msg_args& v);

// Calls on_message and returns the produced error
template <response_handler Handler>
extended_error feed(Handler& h, const any_request_message& msg, std::size_t offset)
{
    extended_error err;
    h.on_message(msg, offset, err);
    return err;
}

}  // namespace nativepg::test

#endif
