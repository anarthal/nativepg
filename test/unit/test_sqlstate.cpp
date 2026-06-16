//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/core/lightweight_test.hpp>

#include <string_view>
#include <system_error>

#include "nativepg/sqlstate.hpp"
#include "nativepg/sqlstate_cond.hpp"

using namespace nativepg;

namespace {

//
// String literal
//
static_assert("00015"_sqlstate == 69);
static_assert("00000"_sqlstate == 0);
static_assert("ZZZZZ"_sqlstate == 596523235);

//
// Runtime parsing
//
void test_runtime_parsing_success()
{
    const auto& cat = get_sqlstate_category();

    // Zero
    BOOST_TEST_EQ(parse_sqlstate("00000"), std::error_code(0, cat));

    // All digits
    BOOST_TEST_EQ(parse_sqlstate("23505"), std::error_code(34361349, cat));

    // Digits and a letter
    BOOST_TEST_EQ(parse_sqlstate("0A000"), std::error_code(2621440, cat));

    // PostgreSQL-specific "P" form
    BOOST_TEST_EQ(parse_sqlstate("42P01"), std::error_code(67735553, cat));

    // High letters
    BOOST_TEST_EQ(parse_sqlstate("HV00B"), std::error_code(293339147, cat));
}

// On error, unknown_sqlstate (error_code -1) is returned
void test_runtime_parsing_error()
{
    const std::error_code invalid(-1, get_sqlstate_category());

    // empty string
    BOOST_TEST_EQ(parse_sqlstate(""), invalid);

    // string has < 5 chars
    BOOST_TEST_EQ(parse_sqlstate("0000"), invalid);

    // string has > 5 chars
    BOOST_TEST_EQ(parse_sqlstate("000000"), invalid);

    // string has lowercase chars
    BOOST_TEST_EQ(parse_sqlstate("0a000"), invalid);

    // string has punctuation
    BOOST_TEST_EQ(parse_sqlstate("0000-"), invalid);
    BOOST_TEST_EQ(parse_sqlstate("42:01"), invalid);

    // string has space/newline/control
    BOOST_TEST_EQ(parse_sqlstate("00 00"), invalid);
    BOOST_TEST_EQ(parse_sqlstate("00\n00"), invalid);
    BOOST_TEST_EQ(parse_sqlstate("00\t00"), invalid);

    // string has NULL characters
    const char with_null[] = {'2', '3', '\0', '0', '5'};
    BOOST_TEST_EQ(parse_sqlstate(std::string_view(with_null, 5)), invalid);

    // string has UTF-8 surrogates
    BOOST_TEST_EQ(parse_sqlstate(std::string_view("val\xc3\xb1", 5)), invalid);

    // string has 0xff chars
    BOOST_TEST_EQ(parse_sqlstate(std::string_view("0000\xff", 5)), invalid);

    // Coverage: we detect errors in all the characters
    BOOST_TEST_EQ(parse_sqlstate("a0000"), invalid);
    BOOST_TEST_EQ(parse_sqlstate("0a000"), invalid);
    BOOST_TEST_EQ(parse_sqlstate("00a00"), invalid);
    BOOST_TEST_EQ(parse_sqlstate("000a0"), invalid);
    BOOST_TEST_EQ(parse_sqlstate("0000a"), invalid);
}

//
// Error category
//

// A std::error_condition can be implicitly created from a sqlstate_cond value
// Casting std::error_condition containing success to bool returns false
// Comparing successful completion to

}  // namespace

int main()
{
    test_runtime_parsing_success();
    test_runtime_parsing_error();

    return boost::report_errors();
}