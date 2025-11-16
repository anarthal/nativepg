//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <string_view>
#include <vector>
#include <array>
#include <cstddef>

#include <boost/core/lightweight_test.hpp>
#include <boost/core/span.hpp>
#include <boost/system/error_code.hpp>


#include "test_utils.hpp"
#include "nativepg/types/pg_bool.hpp"
#include "nativepg/types/pg_int2.hpp"
#include "nativepg/types/pg_int4.hpp"

using boost::system::error_code;
using nativepg::types::pg_oid_type;
using nativepg::types::pg_type_traits;
using nativepg::types::pg_bool;
using nativepg::types::pg_int2;
using nativepg::types::pg_int4;

// Some test cases have been copied from CPython's and Android's base64 test suites

namespace {

void test_pg_bool()
{
    // Setup
    constexpr pg_bool bv_true{true};
    constexpr pg_bool bv_false{false};

    static_assert(bv_true.get() == true);
    static_assert(bv_false.get() == false);

    // Text
    std::string t1 = bv_true.encode_text();  // "t"
    auto bv2 = pg_bool::from_text("true");
    NATIVEPG_TEST_EQ(bv2.get(), true);

    // Binary
    std::string b1 = bv_true.encode_binary();  // "\x01"
    auto bv3 = pg_bool::from_binary(b1);
    NATIVEPG_TEST_EQ(bv3.get(), true);
}

void test_pg_int2()
{
    // Setup
    constexpr pg_int2 x{123};
    static_assert(x.get() == 123);

    std::string txt = x.encode_text();  // "123"
    auto x2 = pg_int2::from_text("99");
    NATIVEPG_TEST_EQ(x2.get(), 99);

    std::string bin = x.encode_binary(); // "\x00{"
    auto x3 = pg_int2::from_binary(bin);
    NATIVEPG_TEST_EQ(x3.get(), 123);
}

void test_pg_int4()
{
    // Setup
    // constexpr usage:
    constexpr pg_int4 answer{42};
    NATIVEPG_TEST_EQ(answer.get(), 42);
    //NATIVEPG_TEST_EQ(pg_int4::oid(), pg_oid_type::int4);
    NATIVEPG_TEST_EQ(pg_int4::byte_length(), 4);
    NATIVEPG_TEST_EQ(pg_int4::supports_binary(), true);
}

}


int main()
{
    test_pg_bool();
    test_pg_int2();
    test_pg_int4();

    return boost::report_errors();
}