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
#include "nativepg/types/traits.hpp"
#include "nativepg/types/concepts.hpp"
#include "test_utils.hpp"

using namespace nativepg::types;
using boost::system::error_code;

namespace {

void test_cpptype_nullable_types()
{
    // Arrange,  Act, and assert all compile time.
    NATIVEPG_TEST(!is_nullable_type_v<int>);
    /* Should hit static assert and fail at compile time

        NATIVEPG_TEST(!is_nullable_type_v<int*>);
        NATIVEPG_TEST(!is_nullable_type_v<std::unique_ptr<int>>);
    */
    NATIVEPG_TEST(!is_nullable_type_v<std::string>);
    NATIVEPG_TEST(!is_nullable_type_v<std::vector<std::byte>>);
    NATIVEPG_TEST(is_nullable_type_v<std::optional<int>>);
    NATIVEPG_TEST(is_nullable_type_v<std::optional<std::string>>);
    NATIVEPG_TEST(is_nullable_type_v<std::optional<std::vector<std::byte>>>);
}

void test_nullable_concept()
{
    // Arrange, Act, Assert (all checks are compile-time)
    static_assert(Nullable<std::optional<int>>);
    static_assert(Nullable<std::optional<std::string>>);
    static_assert(!Nullable<int>);
    static_assert(!Nullable<std::string>);
}

}  // namespace

int main()
{
    test_cpptype_nullable_types();
    test_nullable_concept();

    return boost::report_errors();
}
