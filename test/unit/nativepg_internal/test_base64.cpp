//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/core/lightweight_test.hpp>
#include <boost/core/span.hpp>
#include <boost/system/error_code.hpp>

#include <string_view>
#include <vector>

#include "nativepg_internal/base64.hpp"
#include "test_utils.hpp"

using boost::system::error_code;
using nativepg::protocol::detail::base64_decode;
using nativepg::protocol::detail::base64_encode;

// Some test cases have been copied from CPython's and Android's base64 test suites

namespace {

// TODO: this needs much more cases
struct
{
    std::vector<unsigned char> raw;
    std::string_view encoded;
} success_cases[] = {
    {{0x00},                                                                                           "AA=="        },
    {{0x61},                                                                                           "YQ=="        },
    {{0x61, 0x62},                                                                                     "YWI="        },
    {{0x61, 0x62, 0x63},                                                                               "YWJj"        },
    {{},                                                                                               ""            },
    {

     {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x10, 0x50, 0x60, 0x70, 0x80, 0xab, 0xff, 0x00},
     "AQIDBAUGBwgQUGBwgKv/AA=="
    },
    {{0xff},                                                                                           "/w=="        },
    {{0xff, 0xee},                                                                                     "/+4="        },
    {{0xff, 0xee, 0xdd},                                                                               "/+7d"        },
    {{0xff, 0xee, 0xdd, 0xcc},                                                                         "/+7dzA=="    },
    {{0xff, 0xee, 0xdd, 0xcc, 0xbb},                                                                   "/+7dzLs="    },
    {{0xff, 0xee, 0xdd, 0xcc, 0xbb, 0xaa},                                                             "/+7dzLuq"    },
    {{0xff, 0xee, 0xdd, 0xcc, 0xbb, 0xaa, 0x99},                                                       "/+7dzLuqmQ=="},
    {{0xff, 0xee, 0xdd, 0xcc, 0xbb, 0xaa, 0x99, 0x88},                                                 "/+7dzLuqmYg="},
};

boost::span<const unsigned char> to_span(std::string_view input)
{
    return {reinterpret_cast<const unsigned char*>(input.data()), input.size()};
}

// Regular encode
void test_encode()
{
    for (auto tc : success_cases)
    {
        nativepg::test::context_frame frame(tc.encoded);

        // Setup
        std::vector<unsigned char> dest;

        // Encode
        base64_encode(tc.raw, dest);

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

void test_decode_success()
{
    for (auto tc : success_cases)
    {
        nativepg::test::context_frame frame(tc.encoded);

        // Setup
        std::vector<unsigned char> dest;

        // Decode
        auto ec = base64_decode(to_span(tc.encoded), dest);

        // Check
        NATIVEPG_TEST_EQ(ec, error_code())
        NATIVEPG_TEST_CONT_EQ(dest, tc.raw)
    }
}

}  // namespace

// BOOST_AUTO_TEST_SUITE(base64)

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
    test_decode_success();

    return boost::report_errors();
}