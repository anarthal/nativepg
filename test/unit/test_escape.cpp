//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/assert/source_location.hpp>
#include <boost/core/lightweight_test.hpp>

#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>

#include "nativepg/client_errc.hpp"
#include "nativepg/encoding.hpp"
#include "nativepg/escape.hpp"

using std::error_code;
using namespace std::string_view_literals;
using namespace nativepg;

namespace {

// Calls escape_identifier, recording the individual pieces that the algorithm generates
struct escape_result
{
    error_code ec;
    std::string value;
};

escape_result do_escape_identifier(
    std::string_view input,
    encoding enc = encoding::utf8,
    boost::source_location loc = BOOST_CURRENT_LOCATION
)
{
    escape_result res;
    res.ec = escape_identifier_body(input, enc, [&res, loc](std::string_view piece) {
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
        {"regular",              "my_table",           "my_table"            },
        {"empty",                "",                   ""                    },
        {"spaces",               "my table",           "my table"            },
        {"uppercase",            "MyTable",            "MyTable"             },
        {"single_quote",         "it's",               "it's"                },
        {"backslash",            "a\\b",               "a\\b"                }, // not escaped
        {"tab",                  "a\tb",               "a\tb"                }, // not escaped
        {"newline",              "a\nb",               "a\nb"                }, // not escaped
        {"quote",                "a\"b",               "a\"\"b"              },
        {"only_quote",           "\"",                 "\"\""                },
        {"two_quotes",           "\"\"",               "\"\"\"\""            },
        {"leading_quote",        "\"abc",              "\"\"abc"             },
        {"trailing_quote",       "abc\"",              "abc\"\""             },
        {"several_quotes",       "a\"b\"c",            "a\"\"b\"\"c"         },
        {"non_ascii",            "caf\xc3\xa9",        "caf\xc3\xa9"         },
        {"4byte_sequence",       "\xf0\x9f\x98\x80",   "\xf0\x9f\x98\x80"    },
        {"max_code_point",       "\xf4\x8f\xbf\xbf",   "\xf4\x8f\xbf\xbf"    },
        {"non_ascii_quote",      "\xc3\xa9\"\xc3\xa9", "\xc3\xa9\"\"\xc3\xa9"},

        // Invalid UTF-8 is passed through
        {"lone_continuation",    "\x80",               "\x80"                },
        {"incomplete_2byte",     "\xc3",               "\xc3"                },
        {"overlong",             "\xc0\xaf",           "\xc0\xaf"            },
        {"surrogate",            "\xed\xa0\x80",       "\xed\xa0\x80"        },
        {"invalid_byte_ff",      "\xff",               "\xff"                },
        {"truncated_then_quote", "\xc3\"",             "\xc3\"\""            },
    };

    for (const auto& tc : test_cases)
    {
        auto res = do_escape_identifier(tc.input);
        if (!BOOST_TEST_EQ(res.ec, error_code()) || !BOOST_TEST_EQ(res.value, tc.expected))
            std::cerr << "  In test case: " << tc.name << std::endl;
    }
}

// Encodings other than UTF-8 are not supported, yet
void test_unsupported_encoding()
{
    for (auto enc : {encoding::latin1, encoding::sql_ascii, encoding::sjis})
    {
        auto res = do_escape_identifier("abc", enc);
        BOOST_TEST_EQ(res.ec, error_code(client_errc::unsupported_encoding));
        BOOST_TEST_EQ(res.value, std::string_view());
    }
}

// NULL bytes can't be escaped, and are rejected
void test_null_bytes()
{
    struct
    {
        std::string_view name;
        std::string_view input;
    } test_cases[] = {
        {"only_nul",        "\0"sv           },
        {"leading_nul",     "\0abc"sv        },
        {"middle_nul",      "a\0b"sv         },
        {"trailing_nul",    "abc\0"sv        },
        {"several_nuls",    "a\0b\0c"sv      },
        {"after_quote",     "a\"\0b"sv       },
        {"before_quote",    "a\0\"b"sv       },
        {"after_non_ascii", "caf\xc3\xa9\0"sv},
    };

    for (const auto& tc : test_cases)
    {
        auto res = do_escape_identifier(tc.input);
        if (!BOOST_TEST_EQ(res.ec, error_code(client_errc::null_byte)))
            std::cerr << "  In test case: " << tc.name << std::endl;
    }
}

// The string overload works
void test_string_overload()
{
    std::string dest = "SELECT * FROM \"";  // we append to it
    auto ec = escape_identifier_body("a\"b", encoding::utf8, dest);
    dest += '"';
    BOOST_TEST_EQ(ec, error_code());
    BOOST_TEST_EQ(dest, R"(SELECT * FROM "a""b")");
}

// Strings with other traits/allocators work
template <class T>
struct custom_allocator : std::allocator<char>
{
};

template <class T>
struct custom_traits : std::char_traits<T>
{
};

void test_string_overload_allocator_traits()
{
    std::basic_string<char, custom_traits<char>, custom_allocator<char>>
        dest = "SELECT * FROM \"";  // we append to it
    auto ec = escape_identifier_body("a\"b", encoding::utf8, dest);
    dest += '"';
    std::string_view result{dest.data(), dest.size()};  // the string is not printable
    BOOST_TEST_EQ(ec, error_code());
    BOOST_TEST_EQ(result, R"(SELECT * FROM "a""b")");
}

// The string overload propagates errors
void test_string_overload_error()
{
    std::string dest = "SELECT * FROM \"";
    auto ec = escape_identifier_body("a\0b"sv, encoding::utf8, dest);
    BOOST_TEST_EQ(ec, error_code(client_errc::null_byte));
}

}  // namespace

int main()
{
    test_success();
    test_unsupported_encoding();
    test_null_bytes();

    test_string_overload();
    test_string_overload_allocator_traits();
    test_string_overload_error();

    return boost::report_errors();
}
