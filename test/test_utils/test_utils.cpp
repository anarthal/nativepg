//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/assert.hpp>
#include <boost/assert/source_location.hpp>

#include <string>
#include <string_view>
#include <vector>

#include "nativepg/command_info.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/response_handler.hpp"
#include "printing.hpp"
#include "test_utils.hpp"

namespace {

struct context_frame_data
{
    boost::source_location loc;
    std::string message;
};

thread_local std::vector<context_frame_data> context;

}  // namespace

nativepg::test::context_frame::context_frame(std::string_view message, boost::source_location loc)
{
    context.push_back({loc, std::string(message)});
}

nativepg::test::context_frame::~context_frame()
{
    BOOST_ASSERT(!context.empty());
    context.pop_back();
}

void nativepg::test::print_context()
{
    BOOST_LIGHTWEIGHT_TEST_OSTREAM << "Failure occurred in the following context:\n";
    for (auto it = context.rbegin(); it != context.rend(); ++it)
        BOOST_LIGHTWEIGHT_TEST_OSTREAM << "  " << it->loc << ": " << it->message << '\n';
}

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
