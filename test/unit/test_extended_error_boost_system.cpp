//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/core/lightweight_test.hpp>
#include <boost/system/result.hpp>
#include <boost/system/system_error.hpp>

#include <string>

#include "nativepg/client_errc.hpp"
#include "nativepg/extended_error.hpp"

using namespace nativepg;
using boost::system::error_code;
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
    catch (const boost::system::system_error& err)
    {
        BOOST_TEST_EQ(err.code(), error_code(client_errc::value_too_big));
        BOOST_TEST_EQ(
            std::string_view(err.what()),
            "My message: <unknown nativepg client error> [nativepg.client:4]"
        );  // TODO: update when we implement proper client_errc to string conversion
    }
}

}  // namespace

int main()
{
    test_success();
    test_error();

    return boost::report_errors();
}