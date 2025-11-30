//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/core/lightweight_test.hpp>
#include <boost/core/span.hpp>
#include <boost/system/error_code.hpp>

#include <array>
#include <cstddef>
#include <stdlib.h>
#include <string_view>
#include <vector>

#include "../../test_utils/test_utils.hpp"
#include "nativepg/misc/params.hpp"


using boost::system::error_code;
using nativepg::misc::parse_string_to_pairs;
using nativepg::misc::expand_environment_variables;

// Some test cases have been copied from CPython's and Android's base64 test suites

namespace {

void test_parse_string_to_pairs()
{
    // Arrange / Setup
    std::string test_value = "PG_HOST=localhost;PG_PORT=5432;PG_DATABASE=postgres";

    // Act
    auto subject = parse_string_to_pairs(test_value, false);

    // Assert / Checks
    NATIVEPG_TEST_EQ(subject[0].first, "PG_HOST");
    NATIVEPG_TEST_EQ(subject[0].second, "localhost");

    NATIVEPG_TEST_EQ(subject[1].first, "PG_PORT");
    NATIVEPG_TEST_EQ(subject[1].second, "5432");

    NATIVEPG_TEST_EQ(subject[2].first, "PG_DATABASE");
    NATIVEPG_TEST_EQ(subject[2].second, "postgres");
}

void test_expand_environment_variables()
{
    // Arrange / Setup
    std::string test_value = "PG_HOST=localhost;PG_PORT=5432;PG_PASSWORD=${SECRET}";
    setenv("SECRET", "NOT secret!", 1);

    // Act
    auto subject = parse_string_to_pairs(test_value);

    // Assert / Checks
    NATIVEPG_TEST_EQ(subject[0].first, "PG_HOST");
    NATIVEPG_TEST_EQ(subject[0].second, "localhost");

    NATIVEPG_TEST_EQ(subject[1].first, "PG_PORT");
    NATIVEPG_TEST_EQ(subject[1].second, "5432");

    NATIVEPG_TEST_EQ(subject[2].first, "PG_PASSWORD");
    NATIVEPG_TEST_EQ(subject[2].second, "NOT secret!");
}


}


int main()
{
    test_parse_string_to_pairs();

    test_expand_environment_variables();

    return boost::report_errors();
}