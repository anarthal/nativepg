//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/core/lightweight_test.hpp>
#include <boost/core/span.hpp>

#include <string_view>
#include <vector>

#include "nativepg_internal/base64.hpp"
#include "test_utils.hpp"

using namespace std::string_view_literals;
using boost::system::error_code;
using nativepg::protocol::detail::base64_decode;
using nativepg::protocol::detail::base64_encode;

// Some test cases have been copied from CPython's and Android's base64 test suites

namespace {

// TODO: this needs much more cases
constexpr struct
{
    std::string_view raw;
    std::string_view encoded;
} success_cases[] = {
    {"\0"sv,                                  "AA=="                                  },
    {"a"sv,                                   "YQ=="                                  },
    {"ab"sv,                                  "YWI="                                  },
    {"abc"sv,                                 "YWJj"                                  },
    {""sv,                                    ""                                      },
    {"abcdefghijklmnopqrstuvwxyz"
     "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
     "0123456789!@#0^&*();:<>,. []{}"sv, "YWJjZGVmZ2hpamtsbW5vcHFyc3R1dnd4eXpBQkNE"
     "RUZHSElKS0xNTk9QUVJTVFVWV1hZWjAxMjM0NT"
     "Y3ODkhQCMwXiYqKCk7Ojw+LC4gW117fQ=="},
    {"\xff"sv,                                "/w=="                                  },
    {"\xff\xee"sv,                            "/+4="                                  },
    {"\xff\xee\xdd"sv,                        "/+7d"                                  },
    {"\xff\xee\xdd\xcc"sv,                    "/+7dzA=="                              },
    {"\xff\xee\xdd\xcc\xbb"sv,                "/+7dzLs="                              },
    {"\xff\xee\xdd\xcc\xbb\xaa"sv,            "/+7dzLuq"                              },
    {"\xff\xee\xdd\xcc\xbb\xaa\x99"sv,        "/+7dzLuqmQ=="                          },
    {"\xff\xee\xdd\xcc\xbb\xaa\x99\x88"sv,    "/+7dzLuqmYg="                          },
};

// Regular encode
void test_encode()
{
    for (auto tc : success_cases)
    {
        nativepg::test::context_frame frame(tc.encoded);

        // Setup
        std::vector<unsigned char> dest;
        boost::span<const unsigned char> input(
            reinterpret_cast<const unsigned char*>(tc.raw.data()),
            tc.raw.size()
        );

        // Encode
        base64_encode(input, dest);

        // Check
        NATIVEPG_TEST_CONT_EQ(dest, tc.encoded)
    }
}

// Encoding with a non-empty buffer works
void test_encode_non_empty_buffer()
{
    // Setup
    std::vector<unsigned char> dest{0x01, 0x02, 0x03};
    unsigned char input[]{0x05, 0x09, 0x02, 0x01};
    unsigned char expected[] = {0x01, 0x02, 0x03, 0x42, 0x51, 0x6b, 0x43, 0x41, 0x51, 0x3d, 0x3d};

    // Encode
    base64_encode(input, dest);

    // Check
    NATIVEPG_TEST_CONT_EQ(dest, expected)
}

}  // namespace

// BOOST_AUTO_TEST_SUITE(base64)

// BOOST_AUTO_TEST_CASE(encode) {}

// BOOST_AUTO_TEST_CASE(encode_without_padding)
// {
//     for (auto tc : success_cases)
//     {
//         BOOST_TEST_CONTEXT(tc.encoded)
//         {
//             auto actual = base64_encode(
//                 {reinterpret_cast<const unsigned char*>(tc.raw.data()), tc.raw.size()},
//                 false
//             );
//             BOOST_TEST(actual == tc.encoded_no_padding);
//         }
//     }
// }

// BOOST_AUTO_TEST_CASE(decode_success)
// {
//     for (auto tc : success_cases)
//     {
//         BOOST_TEST_CONTEXT(tc.encoded)
//         {
//             auto actual = base64_decode(tc.encoded);
//             BOOST_TEST_REQUIRE(actual.error() == error_code());
//             auto actual_str = std::string_view(reinterpret_cast<const char*>(actual->data()),
//             actual->size()); BOOST_TEST(actual_str == tc.raw);
//         }
//     }
// }

// BOOST_AUTO_TEST_CASE(decode_success_without_padding)
// {
//     for (auto tc : success_cases)
//     {
//         BOOST_TEST_CONTEXT(tc.encoded_no_padding)
//         {
//             auto actual = base64_decode(tc.encoded_no_padding, false);
//             BOOST_TEST_REQUIRE(actual.error() == error_code());
//             auto actual_str = std::string_view(reinterpret_cast<const char*>(actual->data()),
//             actual->size()); BOOST_TEST(actual_str == tc.raw);
//         }
//     }
// }

// BOOST_AUTO_TEST_CASE(decode_error)
// {
//     constexpr std::string_view cases[] = {
//         // Invalid characters
//         "%3d=="sv,
//         "$3d=="sv,
//         "[=="sv,
//         "YW]3="sv,
//         "3{d=="sv,
//         "3d}=="sv,
//         "@@"sv,
//         "!"sv,
//         "YWJj\n"sv,
//         "YWJj\nYWI="sv,

//         // Bad padding
//         "aGVsbG8sIHdvcmxk="sv,
//         "aGVsbG8sIHdvcmxk=="sv,
//         "aGVsbG8sIHdvcmxkPyE=="sv,
//         "aGVsbG8sIHdvcmxkLg="sv,

//         // Extra bytes
//         "AA==A"sv,
//         "AA==="sv,
//     };

//     for (auto tc : cases)
//     {
//         BOOST_TEST_CONTEXT(tc)
//         {
//             BOOST_TEST(base64_decode(tc).error() == error_code(errc::invalid_base64));
//             BOOST_TEST(base64_decode(tc, false).error() == error_code(errc::invalid_base64));
//         }
//     }
// }

// BOOST_AUTO_TEST_SUITE_END()

int main()
{
    test_encode();
    test_encode_non_empty_buffer();

    return boost::report_errors();
}