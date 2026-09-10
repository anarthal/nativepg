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

#include "nativepg/encoding.hpp"
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

static const char* to_string(nativepg::encoding v)
{
    switch (v)
    {
        case nativepg::encoding::big5: return "big5";
        case nativepg::encoding::euc_cn: return "euc_cn";
        case nativepg::encoding::euc_jp: return "euc_jp";
        case nativepg::encoding::euc_jis_2004: return "euc_jis_2004";
        case nativepg::encoding::euc_kr: return "euc_kr";
        case nativepg::encoding::euc_tw: return "euc_tw";
        case nativepg::encoding::gb18030: return "gb18030";
        case nativepg::encoding::gbk: return "gbk";
        case nativepg::encoding::iso_8859_5: return "iso_8859_5";
        case nativepg::encoding::iso_8859_6: return "iso_8859_6";
        case nativepg::encoding::iso_8859_7: return "iso_8859_7";
        case nativepg::encoding::iso_8859_8: return "iso_8859_8";
        case nativepg::encoding::johab: return "johab";
        case nativepg::encoding::koi8r: return "koi8r";
        case nativepg::encoding::koi8u: return "koi8u";
        case nativepg::encoding::latin1: return "latin1";
        case nativepg::encoding::latin2: return "latin2";
        case nativepg::encoding::latin3: return "latin3";
        case nativepg::encoding::latin4: return "latin4";
        case nativepg::encoding::latin5: return "latin5";
        case nativepg::encoding::latin6: return "latin6";
        case nativepg::encoding::latin7: return "latin7";
        case nativepg::encoding::latin8: return "latin8";
        case nativepg::encoding::latin9: return "latin9";
        case nativepg::encoding::latin10: return "latin10";
        case nativepg::encoding::mule_internal: return "mule_internal";
        case nativepg::encoding::sjis: return "sjis";
        case nativepg::encoding::shift_jis_2004: return "shift_jis_2004";
        case nativepg::encoding::sql_ascii: return "sql_ascii";
        case nativepg::encoding::uhc: return "uhc";
        case nativepg::encoding::utf8: return "utf8";
        case nativepg::encoding::win866: return "win866";
        case nativepg::encoding::win874: return "win874";
        case nativepg::encoding::win1250: return "win1250";
        case nativepg::encoding::win1251: return "win1251";
        case nativepg::encoding::win1252: return "win1252";
        case nativepg::encoding::win1253: return "win1253";
        case nativepg::encoding::win1254: return "win1254";
        case nativepg::encoding::win1255: return "win1255";
        case nativepg::encoding::win1256: return "win1256";
        case nativepg::encoding::win1257: return "win1257";
        case nativepg::encoding::win1258: return "win1258";
        default: return "<unknown encoding>";
    }
}

std::ostream& nativepg::operator<<(std::ostream& os, encoding v) { return os << to_string(v); }

// --- CI server ---
static std::string safe_getenv(const char* name, const char* default_value)
{
    const char* res = std::getenv(name);
    return res ? res : default_value;
}

std::string nativepg::test::get_host() { return safe_getenv("NATIVEPG_SERVER_HOST", "localhost"); }
