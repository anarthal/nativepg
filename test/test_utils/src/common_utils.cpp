//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <cstdlib>
#include <ostream>
#include <string>
#include <string_view>

#include "nativepg/extended_error.hpp"
#include "nativepg/responses/any_request_message.hpp"
#include "nativepg/responses/command_info.hpp"
#include "nativepg/responses/response_handler.hpp"
#include "test_utils/ci_server.hpp"
#include "test_utils/printing.hpp"
#include "test_utils/response_handler_utils.hpp"

// --- Printing ---
std::ostream& nativepg::operator<<(std::ostream& os, const extended_error& err)
{
    return os << "{ .code=" << err.code << ", .diag=" << err.diag << "}";
}

std::ostream& nativepg::operator<<(std::ostream& os, const diagnostics& value)
{
    return os << value.message();
}

std::ostream& nativepg::operator<<(std::ostream& os, const handler_setup_result& value)
{
    if (value.ec)
        return os << "{ .ec=" << value.ec << " }";
    else
        return os << "{ .offset=" << value.offset << " }";
}

std::ostream& nativepg::operator<<(std::ostream& os, const command_info& value)
{
    os << "{ .command_complete_tag=" << value.command_complete_tag << ", .affected_rows=";
    if (value.affected_rows.has_value())
        os << *value.affected_rows;
    else
        os << "<nullopt>";
    return os << ", .portal_suspended=" << value.portal_suspended << " }";
}

static const char* to_string(nativepg::any_request_message::kind value)
{
    using kind = nativepg::any_request_message::kind;

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

std::ostream& nativepg::operator<<(std::ostream& os, any_request_message::kind value)
{
    return os << to_string(value);
}

std::ostream& nativepg::test::operator<<(std::ostream& os, const on_msg_args& v)
{
    return os << "{ " << v.type << ", " << v.offset << " }";
}

// --- CI server ---
static std::string safe_getenv(const char* name, const char* default_value)
{
    const char* res = std::getenv(name);
    return res ? res : default_value;
}

std::string nativepg::test::get_host() { return safe_getenv("NATIVEPG_SERVER_HOST", "localhost"); }
