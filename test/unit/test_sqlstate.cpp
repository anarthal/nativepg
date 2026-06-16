//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/core/lightweight_test.hpp>

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

}  // namespace

int main()
{
    test_runtime_parsing_success();

    return boost::report_errors();
}