//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/core/lightweight_test.hpp>
#include <boost/system/error_code.hpp>

#include <iomanip>
#include <sstream>
#include <string>

#include "nativepg/types/nullable.hpp"
#include "test_utils.hpp"

using namespace nativepg::types;
using boost::system::error_code;

namespace {


void test_cpptype_nullable_types()
{
    // Arrange

    // Act
    constexpr bool res0 = is_nullable_type_v<int>;
    //constexpr bool res1 = is_nullable_type_v<int*>; => Should fail to compile due to static_assert
    constexpr bool res2 = is_nullable_type_v<std::string>;
    constexpr bool res3 = is_nullable_type_v<std::optional<int>>;
    constexpr bool res4 = is_nullable_type_v<std::optional<std::string>>;
    //constexpr bool res5 = is_nullable_type_v<std::unique_ptr<int>>; => Should fail to compile due to static_assert

    // Assert
    NATIVEPG_TEST_EQ(false, res0);
    //NATIVEPG_TEST_EQ(true, res1);
    NATIVEPG_TEST_EQ(false, res2);
    NATIVEPG_TEST_EQ(true, res3);
    NATIVEPG_TEST_EQ(true, res4);
    //NATIVEPG_TEST_EQ(false, res5);
}

void test_unwrap_or_success()
{
    // Arrange
    std::string valid_str = "Primary String";
    std::optional<std::string> valid_opt{valid_str};
    std::string fallback_str = "Fallback String";

    // Act
    // Returns reference wrapper to inside valid_opt. Zero allocation. Zero exception.
    auto res1 = unwrap_or(valid_opt, fallback_str);

    // Assert
    NATIVEPG_TEST_EQ(valid_str, res1.get());
}

void test_assign_to_success()
{
    // Arrange
    std::string valid_str = "Primary String";
    std::optional<std::string> valid_opt{valid_str};
    std::string new_value_str = "Secondary String";

    // Act
    // Returns reference wrapper to inside valid_opt. Zero allocation. Zero exception.
    auto res1 = assign_to(valid_opt, new_value_str);

    // Assert
    NATIVEPG_TEST_EQ(true, res1);
    NATIVEPG_TEST_EQ(new_value_str, valid_opt.value());
}

}  // namespace

int main()
{
    test_cpptype_nullable_types();
    test_unwrap_or_success();
    test_assign_to_success();

    return boost::report_errors();
}
