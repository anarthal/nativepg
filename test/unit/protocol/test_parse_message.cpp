//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/core/lightweight_test.hpp>
#include <boost/core/span.hpp>
#include <boost/system/error_code.hpp>

#include "nativepg/client_errc.hpp"
#include "nativepg/protocol/any_backend_message.hpp"
#include "nativepg/protocol/command_complete.hpp"
#include "nativepg/protocol/parse_message.hpp"

using namespace nativepg;
using boost::system::error_code;
using protocol::parse_message;

namespace {

// A complete message is available
void test_success()
{
    // A command completion message
    const unsigned char data[] =
        {0x43, 0x00, 0x00, 0x00, 0x0d, 0x53, 0x45, 0x4c, 0x45, 0x43, 0x54, 0x20, 0x31, 0x00};

    auto res = parse_message(data);

    BOOST_TEST_EQ(res.ec, error_code());
    BOOST_TEST_EQ(res.size, sizeof(data));
    BOOST_TEST_EQ(res.message.as_command_complete().tag, "SELECT 1");
}

// Short buffers are reported as needs_more, together with the number of missing bytes
void test_incomplete_message()
{
    // A command completion message
    const unsigned char data[] =
        {0x43, 0x00, 0x00, 0x00, 0x0d, 0x53, 0x45, 0x4c, 0x45, 0x43, 0x54, 0x20, 0x31, 0x00};
    boost::span<const unsigned char> msg(data);

    // Empty buffer: we need the 5 header bytes
    auto res = parse_message({});
    BOOST_TEST_EQ(res.ec, error_code(client_errc::needs_more));
    BOOST_TEST_EQ(res.size, 5u);

    // Message type only
    res = parse_message(msg.subspan(0, 1u));
    BOOST_TEST_EQ(res.ec, error_code(client_errc::needs_more));
    BOOST_TEST_EQ(res.size, 4u);

    // Some header bytes
    res = parse_message(msg.subspan(0, 4u));
    BOOST_TEST_EQ(res.ec, error_code(client_errc::needs_more));
    BOOST_TEST_EQ(res.size, 1u);

    // Full header, no body yet
    res = parse_message(msg.subspan(0, 5u));
    BOOST_TEST_EQ(res.ec, error_code(client_errc::needs_more));
    BOOST_TEST_EQ(res.size, 9u);

    // Part of the body
    res = parse_message(msg.subspan(0, 10u));
    BOOST_TEST_EQ(res.ec, error_code(client_errc::needs_more));
    BOOST_TEST_EQ(res.size, 4u);

    // All the body except the last byte
    res = parse_message(msg.subspan(0, 13u));
    BOOST_TEST_EQ(res.ec, error_code(client_errc::needs_more));
    BOOST_TEST_EQ(res.size, 1u);

    // The whole message
    res = parse_message(msg);
    BOOST_TEST_EQ(res.ec, error_code());
    BOOST_TEST_EQ(res.size, sizeof(data));
    BOOST_TEST_EQ(res.message.as_command_complete().tag, "SELECT 1");
}

// Errors
void test_error_unknown_message_type()
{
    // This message type is unknown
    const unsigned char data[] = {0xff, 0x00, 0x00, 0x00, 0x01, 0x00};

    auto res = parse_message(data);
    BOOST_TEST_EQ(res.ec, error_code(client_errc::protocol_value_error));
}

void test_error_invalid_length()
{
    // Length -1
    const unsigned char data[] = {0x43, 0xff, 0xff, 0xff, 0xff};

    auto res = parse_message(data);
    BOOST_TEST_EQ(res.ec, error_code(client_errc::protocol_value_error));
}

// TODO: error when deserializing message

}  // namespace

int main()
{
    test_success();
    test_incomplete_message();

    test_error_unknown_message_type();
    test_error_invalid_length();

    return boost::report_errors();
}
