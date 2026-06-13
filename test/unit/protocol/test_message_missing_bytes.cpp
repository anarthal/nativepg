//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/core/lightweight_test.hpp>
#include <boost/core/span.hpp>

#include "nativepg/protocol/parse_message.hpp"

using namespace nativepg;
using protocol::message_missing_bytes;

namespace {

// A complete message needs no further bytes
void test_complete()
{
    // A command completion message
    const unsigned char data[] =
        {0x43, 0x00, 0x00, 0x00, 0x0d, 0x53, 0x45, 0x4c, 0x45, 0x43, 0x54, 0x20, 0x31, 0x00};

    BOOST_TEST_EQ(message_missing_bytes(data), 0u);
}

// Partial buffers report how many bytes are still missing
void test_incomplete_message()
{
    // A command completion message
    const unsigned char data[] =
        {0x43, 0x00, 0x00, 0x00, 0x0d, 0x53, 0x45, 0x4c, 0x45, 0x43, 0x54, 0x20, 0x31, 0x00};
    boost::span<const unsigned char> msg(data);

    // Empty buffer: we need the 5 header bytes
    BOOST_TEST_EQ(message_missing_bytes({}), 5u);

    // Message type only
    BOOST_TEST_EQ(message_missing_bytes(msg.subspan(0, 1u)), 4u);

    // Some header bytes
    BOOST_TEST_EQ(message_missing_bytes(msg.subspan(0, 4u)), 1u);

    // Full header, no body yet
    BOOST_TEST_EQ(message_missing_bytes(msg.subspan(0, 5u)), 9u);

    // Part of the body
    BOOST_TEST_EQ(message_missing_bytes(msg.subspan(0, 10u)), 4u);

    // All the body except the last byte
    BOOST_TEST_EQ(message_missing_bytes(msg.subspan(0, 13u)), 1u);

    // The whole message
    BOOST_TEST_EQ(message_missing_bytes(msg), 0u);
}

// A bad header is reported as complete
void test_invalid_header()
{
    // Length -1
    const unsigned char data[] = {0x43, 0xff, 0xff, 0xff, 0xff};

    BOOST_TEST_EQ(message_missing_bytes(data), 0u);
}

// We don't attempt to parse the message contents at this point
void test_bad_message_body()
{
    // This message type is unknown
    const unsigned char data[] = {0xff, 0x00, 0x00, 0x00, 0x01, 0x00};

    BOOST_TEST_EQ(message_missing_bytes(data), 0u);
}

}  // namespace

int main()
{
    test_complete();
    test_incomplete_message();

    test_invalid_header();
    test_bad_message_body();

    return boost::report_errors();
}
