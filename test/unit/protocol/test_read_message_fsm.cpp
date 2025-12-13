//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/core/lightweight_test.hpp>
#include <boost/core/span.hpp>
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

    // Setup
    read_message_fsm fsm;

    // Call the function
    auto act = fsm.resume(data);

    // Check
    BOOST_TEST_EQ(act.type(), read_message_fsm::result_type::message);
    BOOST_TEST_EQ(get<protocol::command_complete>(act.message()).tag, "SELECT 1");
}

// Short reads are correctly handled
void test_short_reads()
{
    // A command completion message
    const unsigned char data[] =
        {0x43, 0x00, 0x00, 0x00, 0x0d, 0x53, 0x45, 0x4c, 0x45, 0x43, 0x54, 0x20, 0x31, 0x00};
    boost::span<const unsigned char> msg(data);

    // Setup
    read_message_fsm fsm;

    // Empty reads don't cause harm
    auto act = fsm.resume({});
    BOOST_TEST_EQ(act.type(), read_message_fsm::result_type::needs_more);
    BOOST_TEST_EQ(act.hint(), 5u);

    // Message type
    act = fsm.resume(msg.subspan(0, 1u));
    BOOST_TEST_EQ(act.type(), read_message_fsm::result_type::needs_more);
    BOOST_TEST_EQ(act.hint(), 4u);

    // Some header bytes
    act = fsm.resume(msg.subspan(0, 4u));
    BOOST_TEST_EQ(act.type(), read_message_fsm::result_type::needs_more);
    BOOST_TEST_EQ(act.hint(), 1u);

    // Full header
    act = fsm.resume(msg.subspan(0, 5u));
    BOOST_TEST_EQ(act.type(), read_message_fsm::result_type::needs_more);
    BOOST_TEST_EQ(act.hint(), 9u);

    // Part of the body
    act = fsm.resume(msg.subspan(0, 10u));
    BOOST_TEST_EQ(act.type(), read_message_fsm::result_type::needs_more);
    BOOST_TEST_EQ(act.hint(), 4u);

    // All the body except the last byte
    act = fsm.resume(msg.subspan(0, 13u));
    BOOST_TEST_EQ(act.type(), read_message_fsm::result_type::needs_more);
    BOOST_TEST_EQ(act.hint(), 1u);

    // Last byte
    act = fsm.resume(msg);
    BOOST_TEST_EQ(act.type(), read_message_fsm::result_type::message);
    BOOST_TEST_EQ(get<protocol::command_complete>(act.message()).tag, "SELECT 1");
}

}  // namespace

int main()
{
    test_success();
    test_short_reads();

    return boost::report_errors();
}
