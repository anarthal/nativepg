//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/core/lightweight_test.hpp>
#include <boost/system/error_code.hpp>

#include "nativepg/request.hpp"
#include "nativepg_internal/check_request.hpp"

using namespace nativepg;
using boost::system::error_code;
using protocol::detail::check_request;

namespace {

void test_success()
{
    request req;
    req.add_query("SELECT $1", {42});

    BOOST_TEST_EQ(check_request(req), error_code());
}

}  // namespace

int main()
{
    test_success();

    return boost::report_errors();
}
