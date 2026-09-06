//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_TEST_RESPONSE_MSG_TYPE_HPP
#define NATIVEPG_TEST_RESPONSE_MSG_TYPE_HPP

#include <cstddef>
#include <ostream>

#include "nativepg/responses/any_request_message.hpp"
#include "nativepg/responses/response_handler.hpp"

namespace nativepg::test {

inline const char* to_string(any_request_message::kind value)
{
    using kind = any_request_message::kind;

    switch (value)
    {
        case kind::bind_complete: return "bind_complete";
        case kind::close_complete: return "close_complete";
        case kind::command_complete: return "command_complete";
        case kind::data_row: return "data_row";
        case kind::parameter_description: return "parameter_description";
        case kind::row_description: return "row_description";
        case kind::empty_query_response: return "empty_query_response";
        case kind::portal_suspended: return "portal_suspended";
        case kind::error_response: return "error_response";
        case kind::parse_complete: return "parse_complete";
        case kind::message_skipped: return "message_skipped";
        default: return "<unknown any_request_message::kind>";
    }
}

struct on_msg_args
{
    any_request_message::kind type;
    std::size_t offset;

    friend bool operator==(const on_msg_args&, const on_msg_args&) = default;
    friend std::ostream& operator<<(std::ostream& os, const on_msg_args& v)
    {
        return os << "{ " << to_string(v.type) << ", " << v.offset << " }";
    }
};

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
