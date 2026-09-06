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

enum class response_msg_type
{
    bind_complete,
    close_complete,
    command_complete,
    data_row,
    parameter_description,
    row_description,
    empty_query_response,
    portal_suspended,
    error_response,
    parse_complete,
    message_skipped,
};

inline const char* to_string(response_msg_type value)
{
    switch (value)
    {
        case response_msg_type::bind_complete: return "bind_complete";
        case response_msg_type::close_complete: return "close_complete";
        case response_msg_type::command_complete: return "command_complete";
        case response_msg_type::data_row: return "data_row";
        case response_msg_type::parameter_description: return "parameter_description";
        case response_msg_type::row_description: return "row_description";
        case response_msg_type::empty_query_response: return "empty_query_response";
        case response_msg_type::portal_suspended: return "portal_suspended";
        case response_msg_type::error_response: return "error_response";
        case response_msg_type::parse_complete: return "parse_complete";
        case response_msg_type::message_skipped: return "message_skipped";
        default: return "<unknown response_msg_type>";
    }
}

inline std::ostream& operator<<(std::ostream& os, response_msg_type t) { return os << to_string(t); }

inline response_msg_type to_type(const any_request_message& msg)
{
    using kind = any_request_message::kind;

    switch (msg.type())
    {
        case kind::bind_complete: return response_msg_type::bind_complete;
        case kind::close_complete: return response_msg_type::close_complete;
        case kind::command_complete: return response_msg_type::command_complete;
        case kind::data_row: return response_msg_type::data_row;
        case kind::parameter_description: return response_msg_type::parameter_description;
        case kind::row_description: return response_msg_type::row_description;
        case kind::empty_query_response: return response_msg_type::empty_query_response;
        case kind::portal_suspended: return response_msg_type::portal_suspended;
        case kind::error_response: return response_msg_type::error_response;
        case kind::parse_complete: return response_msg_type::parse_complete;
        default: return response_msg_type::message_skipped;
    }
}

struct on_msg_args
{
    response_msg_type type;
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
