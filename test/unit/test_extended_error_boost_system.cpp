//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/core/lightweight_test.hpp>
#include <boost/system/result.hpp>
#include <system_error>

#include <string>

#include "nativepg/client_errc.hpp"
#include "nativepg/extended_error.hpp"

using namespace nativepg;
using std::error_code;
using boost::system::result;

// extended_error can be used with boost::system::result

namespace {

void test_success()
{
    result<int, extended_error> result{42};
    BOOST_TEST_EQ(result.value(), 42);
}

void test_error()
{
    result<int, extended_error> result{
        extended_error{.code = client_errc::value_too_big, .diag = std::string("My message")}
    };

    try
    {
        result.value();
        BOOST_TEST(false);
    }
    catch (const std::system_error& err)
    {
        BOOST_TEST_EQ(err.code(), error_code(client_errc::value_too_big));
        BOOST_TEST_EQ(std::string_view(err.what()), "My message: value_too_big");
    }
}

}  // namespace

int main()
{
    test_success();
    test_error();

    return boost::report_errors();
}