//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <cstdlib>
#include <string>
#include <string_view>

#include "nativepg/command_info.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/response_handler.hpp"
#include "test_utils/ci_server.hpp"
#include "test_utils/printing.hpp"

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

static std::string safe_getenv(const char* name, const char* default_value)
{
    const char* res = std::getenv(name);
    return res ? res : default_value;
}

// --- CI server ---
std::string nativepg::test::get_host() { return safe_getenv("NATIVEPG_SERVER_HOST", "localhost"); }
