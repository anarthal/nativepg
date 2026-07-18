//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/core/lightweight_test.hpp>
#include <boost/system/error_code.hpp>

#include <iomanip>
#include <memory>
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
    // constexpr bool res1 = is_nullable_type_v<int*>; => Should fail to compile due to static_assert
    constexpr bool res2 = is_nullable_type_v<std::string>;
    constexpr bool res3 = is_nullable_type_v<std::optional<int>>;
    constexpr bool res4 = is_nullable_type_v<std::optional<std::string>>;
    // constexpr bool res5 = is_nullable_type_v<std::unique_ptr<int>>; => Should fail to compile due to
    // static_assert

    // Assert
    NATIVEPG_TEST_EQ(false, res0);
    // NATIVEPG_TEST_EQ(true, res1);
    NATIVEPG_TEST_EQ(false, res2);
    NATIVEPG_TEST_EQ(true, res3);
    NATIVEPG_TEST_EQ(true, res4);
    // NATIVEPG_TEST_EQ(false, res5);
}

void test_is_nullable_ptr_trait()
{
    // Arrange

    // Act
    constexpr bool res_raw_ptr = is_nullable_ptr_v<int*>;
    constexpr bool res_unique_ptr = is_nullable_ptr_v<std::unique_ptr<int>>;
    constexpr bool res_shared_ptr = is_nullable_ptr_v<std::shared_ptr<int>>;
    constexpr bool res_optional = is_nullable_ptr_v<std::optional<int>>;
    constexpr bool res_int = is_nullable_ptr_v<int>;
    constexpr bool res_string = is_nullable_ptr_v<std::string>;

    // Assert

    // Raw pointers are explicitly excluded, even though they support nullptr operations
    NATIVEPG_TEST_EQ(false, res_raw_ptr);

    // Smart pointers support nullptr operations and are not raw pointers
    NATIVEPG_TEST_EQ(true, res_unique_ptr);
    NATIVEPG_TEST_EQ(true, res_shared_ptr);

    // std::optional has no nullptr_t constructor/assignment/comparison, so it doesn't qualify
    NATIVEPG_TEST_EQ(false, res_optional);

    // Regular types don't support nullptr operations at all
    NATIVEPG_TEST_EQ(false, res_int);
    NATIVEPG_TEST_EQ(false, res_string);
}

void test_is_optional_trait()
{
    // Arrange

    // Act
    constexpr bool res_optional_int = is_optional_v<std::optional<int>>;
    constexpr bool res_optional_string = is_optional_v<std::optional<std::string>>;
    constexpr bool res_int = is_optional_v<int>;
    constexpr bool res_unique_ptr = is_optional_v<std::unique_ptr<int>>;

    // Assert
    NATIVEPG_TEST_EQ(true, res_optional_int);
    NATIVEPG_TEST_EQ(true, res_optional_string);
    NATIVEPG_TEST_EQ(false, res_int);
    NATIVEPG_TEST_EQ(false, res_unique_ptr);
}

void test_nullable_concept()
{
    // Arrange, Act, Assert (all checks are compile-time)
    static_assert(Nullable<std::optional<int>>);
    static_assert(Nullable<std::optional<std::string>>);
    static_assert(!Nullable<int>);
    static_assert(!Nullable<std::string>);
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

void test_unwrap_or_null()
{
    // Arrange
    std::optional<std::string> null_opt;
    std::string fallback_str = "Fallback String";

    // Act
    // The optional has no value, so the fallback should be returned instead
    auto res1 = unwrap_or(null_opt, fallback_str);

    // Assert
    NATIVEPG_TEST_EQ(fallback_str, res1.get());
}

void test_unwrap_or_int()
{
    // Arrange
    std::optional<int> valid_opt{42};
    std::optional<int> null_opt;
    int fallback = -1;

    // Act
    auto res_valid = unwrap_or(valid_opt, fallback);
    auto res_null = unwrap_or(null_opt, fallback);

    // Assert
    NATIVEPG_TEST_EQ(42, res_valid.get());
    NATIVEPG_TEST_EQ(fallback, res_null.get());
}

void test_unwrap_or_bool()
{
    // Arrange
    std::optional<bool> valid_opt{true};
    std::optional<bool> null_opt;
    bool fallback = false;

    // Act
    auto res_valid = unwrap_or(valid_opt, fallback);
    auto res_null = unwrap_or(null_opt, fallback);

    // Assert
    NATIVEPG_TEST_EQ(true, res_valid.get());
    NATIVEPG_TEST_EQ(false, res_null.get());
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

void test_assign_to_null()
{
    // Arrange
    std::optional<std::string> null_opt;
    std::string target = "Unchanged";

    // Act
    // The optional has no value, so the assignment should be skipped
    auto res1 = assign_to(null_opt, target);

    // Assert
    NATIVEPG_TEST_EQ(false, res1);
    NATIVEPG_TEST_EQ(std::string("Unchanged"), target);
}

void test_assign_to_int()
{
    // Arrange
    std::optional<int> valid_opt{7};
    std::optional<int> null_opt;
    int target = -1;

    // Act
    auto res_valid = assign_to(valid_opt, target);

    // Assert
    NATIVEPG_TEST_EQ(true, res_valid);
    NATIVEPG_TEST_EQ(7, target);

    // Act (reset target, exercise the null path)
    target = -1;
    auto res_null = assign_to(null_opt, target);

    // Assert
    NATIVEPG_TEST_EQ(false, res_null);
    NATIVEPG_TEST_EQ(-1, target);
}

}  // namespace

int main()
{
    test_cpptype_nullable_types();
    test_is_nullable_ptr_trait();
    test_is_optional_trait();
    test_nullable_concept();
    test_unwrap_or_success();
    test_unwrap_or_null();
    test_unwrap_or_int();
    test_unwrap_or_bool();
    test_assign_to_success();
    test_assign_to_null();
    test_assign_to_int();

    return boost::report_errors();
}
