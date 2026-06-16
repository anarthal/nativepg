//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/core/lightweight_test.hpp>

#include <span>

#include "nativepg/field_view.hpp"
#include "test_utils.hpp"

using namespace nativepg;

namespace {

// Default ctor builds a NULL value
void test_default_constructor()
{
    field_view f;
    BOOST_TEST(f.is_null());
    NATIVEPG_TEST_CONT_EQ(f.data(), std::span<const unsigned char>{});
}

// Constructor

}  // namespace

int main()
{
    test_default_constructor();

    return boost::report_errors();
}