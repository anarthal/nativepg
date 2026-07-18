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

#include "nativepg/types/traits.hpp"
#include "test_utils.hpp"

using namespace nativepg::types;
using boost::system::error_code;

namespace {


void test_check_unique_ptr()
{
    NATIVEPG_TEST(is_unique_ptr<std::unique_ptr<int>>());
    NATIVEPG_TEST(!is_unique_ptr<int>());
}

void test_check_shared_ptr()
{
    NATIVEPG_TEST(is_shared_ptr<std::shared_ptr<int>>());
    NATIVEPG_TEST(!is_shared_ptr<int>());
}

void test_check_weak_ptr()
{
    NATIVEPG_TEST(is_weak_ptr<std::weak_ptr<int>>());
    NATIVEPG_TEST(!is_weak_ptr<int>());
}

void test_check_optional()
{
    NATIVEPG_TEST(is_optional<std::optional<int>>());
    NATIVEPG_TEST(!is_optional<int>());
}

}


int main()
{
    test_check_unique_ptr();
    test_check_shared_ptr();
    test_check_weak_ptr();
    test_check_optional();

    return boost::report_errors();
}
