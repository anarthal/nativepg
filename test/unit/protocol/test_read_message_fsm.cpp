//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/core/lightweight_test.hpp>
#include <boost/variant2/variant.hpp>

#include <ostream>

#include "nativepg/protocol/command_complete.hpp"
#include "nativepg/protocol/read_message_fsm.hpp"

using namespace nativepg;
using boost::variant2::get;
using protocol::read_message_fsm;

// Operators
static const char* to_string(read_message_fsm::result_type t)
{
    switch (t)
    {
        case read_message_fsm::result_type::needs_more: return "needs_more";
        case read_message_fsm::result_type::error: return "error";
        case read_message_fsm::result_type::message: return "message";
        default: return "<unknown read_message_fsm::result_type>";
    }
}

namespace nativepg::protocol {

std::ostream& operator<<(std::ostream& os, read_message_fsm::result_type t) { return os << to_string(t); }

}  // namespace nativepg::protocol

namespace {

// A message is already available
void test_success()
{
    // A command completion message
    const unsigned char data[] =
        {0x43, 0x00, 0x00, 0x00, 0x0d, 0x53, 0x45, 0x4c, 0x45, 0x43, 0x54, 0x20, 0x31, 0x00};

    read_message_fsm fsm;
    auto act = fsm.resume(data);
    BOOST_TEST_EQ(act.type(), read_message_fsm::result_type::message);
    BOOST_TEST_EQ(get<protocol::command_complete>(act.message()).tag, "SELECT 1");
}

}  // namespace

int main()
{
    test_success();

    return boost::report_errors();
}
