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

#include "nativepg/extended_error.hpp"
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
    return os << "{ .code=" << err.code << ", .diag=" << err.diag.message() << "}";
}
