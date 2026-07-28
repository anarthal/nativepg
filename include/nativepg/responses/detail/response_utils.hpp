//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_RESPONSE_UTILS_HPP
#define NATIVEPG_RESPONSE_UTILS_HPP

#include "nativepg/protocol/command_complete_tag.hpp"
#include "nativepg/responses/command_info.hpp"
#include "nativepg/responses/response_handler.hpp"
#include "nativepg/sqlstate.hpp"

namespace nativepg::detail {

handler_setup_result resultset_setup(const request& req, std::size_t offset);

inline void store_error(const protocol::error_response& err, extended_error& to)
{
    to.code = parse_sqlstate(err.sqlstate.value_or(std::string_view{}));
    to.diag.assign(err);
}

inline void maybe_store_error(const protocol::error_response& err, extended_error& to)
{
    if (!to.code)
        store_error(err, to);
}

inline void maybe_store_error(const any_request_message& msg, extended_error& to)
{
    const auto* err = boost::variant2::get_if<protocol::error_response>(&msg);
    if (err)
        maybe_store_error(*err, to);
}

inline void reset_info(command_info& obj)
{
    obj.command_complete_tag.clear();
    obj.affected_rows.reset();
    obj.portal_suspended = false;
}

inline void from_command_complete(command_info& obj, protocol::command_complete msg)
{
    obj.command_complete_tag.assign(msg.tag);

    // Parsing the command tag is best effort. The protocol does not
    // enforce the format. Extensions and third party tools may send non-conforming tags,
    // and the format may change in slightly incompatible ways in the future.
    // libpq does the same.
    auto ec = protocol::parse_command_complete_tag(msg.tag, obj.affected_rows);
    static_cast<void>(ec);
}

}  // namespace nativepg::detail

#endif
