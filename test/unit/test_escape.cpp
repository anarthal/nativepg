//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/assert/source_location.hpp>
#include <boost/core/lightweight_test.hpp>

#include <iostream>
#include <string>
#include <string_view>
#include <system_error>

#include "nativepg/client_errc.hpp"
#include "nativepg/encoding.hpp"
#include "nativepg/escape.hpp"

using std::error_code;
using namespace nativepg;

namespace {

// Calls escape_identifier, recording the individual pieces that the algorithm generates
struct escape_result
{
    error_code ec;
    std::string value;
};

escape_result escape(
    std::string_view input,
    encoding enc = encoding::utf8,
    boost::source_location loc = BOOST_CURRENT_LOCATION
)
{
    escape_result res;
    res.ec = escape_identifier(input, enc, [&res, loc](std::string_view piece) {
        // Pieces should never be empty
        if (!BOOST_TEST_NOT(piece.empty()))
            std::cerr << "  Called from " << loc << std::endl;
        res.value += piece;
    });
    return res;
}

void test_success()
{
    struct
    {
        std::string_view name;
        std::string_view input;
        std::string_view expected;
    } test_cases[] = {
        {"regular", "my_table", "my_table"},
        {"empty", "", ""},
        {"spaces", "my table", "my table"},
        {"uppercase", "MyTable", "MyTable"},
        {"single_quote", "it's", "it's"},
        {"backslash", "a\\b", "a\\b"},
        {"quote", "a\"b", "a\"\"b"},
        {"only_quote", "\"", "\"\""},
        {"two_quotes", "\"\"", "\"\"\"\""},
        {"leading_quote", "\"abc", "\"\"abc"},
        {"trailing_quote", "abc\"", "abc\"\""},
        {"several_quotes", "a\"b\"c", "a\"\"b\"\"c"},
        {"nul", std::string_view("a\0b", 3), std::string_view("a\0b", 3)},
        {"non_ascii", "caf\xc3\xa9", "caf\xc3\xa9"},
        {"4byte_sequence", "\xf0\x9f\x98\x80", "\xf0\x9f\x98\x80"},
        {"max_code_point", "\xf4\x8f\xbf\xbf", "\xf4\x8f\xbf\xbf"},
        {"non_ascii_quote", "\xc3\xa9\"\xc3\xa9", "\xc3\xa9\"\"\xc3\xa9"},
    };

    for (const auto& tc : test_cases)
    {
        auto res = escape(tc.input);
        if (!BOOST_TEST_EQ(res.ec, error_code()) || !BOOST_TEST_EQ(res.value, tc.expected))
            std::cerr << "  In test case: " << tc.name << std::endl;
    }
}

// The escaped value can also be appended to a string. The caller adds the quotes
void test_string_overload()
{
    std::string dest = "SELECT * FROM \"";
    auto ec = escape_identifier("a\"b", encoding::utf8, dest);
    dest += '"';
    BOOST_TEST_EQ(ec, error_code());
    BOOST_TEST_EQ(dest, R"(SELECT * FROM "a""b")");
}

// Encodings other than UTF-8 are not supported, yet
void test_unsupported_encoding()
{
    for (auto enc : {encoding::latin1, encoding::sql_ascii, encoding::sjis})
    {
        auto res = escape("abc", enc);
        BOOST_TEST_EQ(res.ec, error_code(client_errc::unsupported_encoding));
        BOOST_TEST_EQ(res.value, std::string_view());
    }
}

// We don't validate UTF-8: malformed sequences are passed through unchanged.
// Since no byte in a multi-byte sequence can be mistaken for a quote,
// this doesn't compromise the escaping
void test_malformed_utf8_passthrough()
{
    struct
    {
        std::string_view name;
        std::string_view input;
        std::string_view expected;
    } test_cases[] = {
        {"lone_continuation",    "\x80",         "\x80"        },
        {"incomplete_2byte",     "\xc3",         "\xc3"        },
        {"overlong",             "\xc0\xaf",     "\xc0\xaf"    },
        {"surrogate",            "\xed\xa0\x80", "\xed\xa0\x80"},
        {"invalid_byte_ff",      "\xff",         "\xff"        },
        {"truncated_then_quote", "\xc3\"",       "\xc3\"\""    },
    };

    for (const auto& tc : test_cases)
    {
        auto res = escape(tc.input);
        if (!BOOST_TEST_EQ(res.ec, error_code()) || !BOOST_TEST_EQ(res.value, tc.expected))
            std::cerr << "  In test case: " << tc.name << std::endl;
    }
}

}  // namespace

int main()
{
    test_success();
    test_string_overload();
    test_unsupported_encoding();
    test_malformed_utf8_passthrough();

    return boost::report_errors();
}
