//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_TEST_RESPONSE_MSG_TYPE_HPP
#define NATIVEPG_TEST_RESPONSE_MSG_TYPE_HPP

#include <boost/variant2/variant.hpp>

#include <ostream>

#include "nativepg/response_handler.hpp"

namespace nativepg::test {

// TODO: duplicated
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
    struct visitor
    {
        // clang-format off
        response_msg_type operator()(const protocol::bind_complete&) const { return response_msg_type::bind_complete;}
        response_msg_type operator()(const protocol::close_complete&) const { return response_msg_type::close_complete;}
        response_msg_type operator()(const protocol::command_complete&) const { return response_msg_type::command_complete;}
        response_msg_type operator()(const protocol::data_row&) const { return response_msg_type::data_row;}
        response_msg_type operator()(const protocol::parameter_description&) const { return response_msg_type::parameter_description;}
        response_msg_type operator()(const protocol::row_description&) const { return response_msg_type::row_description;}
        response_msg_type operator()(const protocol::empty_query_response&) const { return response_msg_type::empty_query_response;}
        response_msg_type operator()(const protocol::portal_suspended&) const { return response_msg_type::portal_suspended;}
        response_msg_type operator()(const protocol::error_response&) const { return response_msg_type::error_response;}
        response_msg_type operator()(const protocol::parse_complete&) const { return response_msg_type::parse_complete;}
        response_msg_type operator()(const message_skipped&) const { return response_msg_type::message_skipped;}
        // clang-format on
    };

    return boost::variant2::visit(visitor{}, msg);
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

}  // namespace nativepg::test

#endif
