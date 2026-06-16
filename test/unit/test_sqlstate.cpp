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
    BOOST_TEST_EQ(parse_sqlstate("00000"), std::error_code(0, cat));
}

}  // namespace

int main()
{
    test_runtime_parsing_success();

    return boost::report_errors();
}