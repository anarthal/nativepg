//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/assert/source_location.hpp>
#include <boost/core/lightweight_test.hpp>
#include <boost/system/error_code.hpp>

#include <initializer_list>
#include <ostream>
#include <source_location>

#include "nativepg/request.hpp"
#include "test_utils.hpp"

using namespace nativepg;
using detail::request_msg_type;

namespace nativepg::detail {

std::ostream& operator<<(std::ostream& os, request_msg_type type)
{
    switch (type)
    {
    case request_msg_type::bind: return os << "bind";
    case request_msg_type::close: return os << "close";
    case request_msg_type::describe: return os << "describe";
    case request_msg_type::execute: return os << "execute";
    case request_msg_type::flush: return os << "flush";
    case request_msg_type::parse: return os << "parse";
    case request_msg_type::query: return os << "query";
    case request_msg_type::sync: return os << "sync";
    default: return os << "<unknown request_msg_type>";
    }
}

}  // namespace nativepg::detail

namespace {

void check_payload(
    const request& req,
    std::initializer_list<unsigned char> expected,
    std::source_location loc = std::source_location::current()
)
{
    test::context_frame frame{loc};
    NATIVEPG_TEST_CONT_EQ(req.payload(), expected);
}

void check_messages(
    const request& req,
    std::initializer_list<request_msg_type> expected,
    std::source_location loc = std::source_location::current()
)
{
    test::context_frame frame{loc};
    NATIVEPG_TEST_CONT_EQ(detail::request_access::messages(req), expected);
}

void test_simple_query()
{
    request req;
    req.add_simple_query("select 1;");

    check_payload(
        req,
        {0x51, 0x00, 0x00, 0x00, 0x0e, 0x73, 0x65, 0x6c, 0x65, 0x63, 0x74, 0x20, 0x31, 0x3b, 0x00}
    );
    check_messages(req, {request_msg_type::query});
}

}  // namespace

int main()
{
    test_simple_query();

    return boost::report_errors();
}