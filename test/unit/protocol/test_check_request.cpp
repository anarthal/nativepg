//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/core/lightweight_test.hpp>
#include <boost/system/error_code.hpp>

#include "nativepg/client_errc.hpp"
#include "nativepg/request.hpp"
#include "nativepg_internal/check_request.hpp"

using namespace nativepg;
using boost::system::error_code;
using protocol::detail::check_request;

namespace {

void test_success()
{
    {
        // Extended protocol: parse, bind, describe, execute, sync
        request req;
        req.add_query("SELECT $1", {42});

        BOOST_TEST_EQ(check_request(req), error_code());
    }
    {
        // Extended protocol: (parse, bind, describe, execute)x2, sync
        request req(false);
        req.add_query("SELECT $1", {42});
        req.add_query("SELECT $2", {50});
        req.add(protocol::sync{});

        BOOST_TEST_EQ(check_request(req), error_code());
    }
    {
        // Extended protocol: sync
        request req(false);
        req.add(protocol::sync{});

        BOOST_TEST_EQ(check_request(req), error_code());
    }
    {
        // Simple protocol: query
        request req;
        req.add_simple_query("SELECT 1");

        BOOST_TEST_EQ(check_request(req), error_code());
    }
    {
        // Extended protocol: (parse, bind, describe, execute, sync)x2
        request req;
        req.add_query("SELECT $1", {42});
        req.add_query("SELECT $1", {42});

        BOOST_TEST_EQ(check_request(req), error_code());
    }
    {
        // Simple protocol: query x2
        request req;
        req.add_simple_query("SELECT 1");
        req.add_simple_query("SELECT 2");

        BOOST_TEST_EQ(check_request(req), error_code());
    }
    {
        // Mixing: (parse, bind, describe, execute, sync), query, (parse, bind, describe, execute, sync)
        request req;
        req.add_query("SELECT $1", {42});
        req.add_simple_query("SELECT 1");
        req.add_query("SELECT $1", {42});

        BOOST_TEST_EQ(check_request(req), error_code());
    }
    {
        // Mixing: query, (parse, bind, describe, execute, sync)
        request req;
        req.add_simple_query("SELECT 1");
        req.add_query("SELECT $1", {42});

        BOOST_TEST_EQ(check_request(req), error_code());
    }
    {
        // Mixing: query, (parse, bind, describe, execute, sync), query
        request req;
        req.add_simple_query("SELECT 1");
        req.add_query("SELECT $1", {42});
        req.add_simple_query("SELECT 2");

        BOOST_TEST_EQ(check_request(req), error_code());
    }
    {
        // Extended protocol: bind, sync
        request req(false);
        req.add_bind("stmt", {});
        req.add(protocol::sync{});

        BOOST_TEST_EQ(check_request(req), error_code());
    }
    {
        // Extended protocol: close, sync
        request req(false);
        req.add(protocol::close{protocol::portal_or_statement::statement, "stmt"});
        req.add(protocol::sync{});

        BOOST_TEST_EQ(check_request(req), error_code());
    }
    {
        // Extended protocol: describe, sync
        request req(false);
        req.add(protocol::describe{protocol::portal_or_statement::statement, "stmt"});
        req.add(protocol::sync{});

        BOOST_TEST_EQ(check_request(req), error_code());
    }
    {
        // Extended protocol: execute, sync
        request req(false);
        req.add(protocol::execute{.portal_name = {}, .max_num_rows = 0});
        req.add(protocol::sync{});

        BOOST_TEST_EQ(check_request(req), error_code());
    }
    {
        // Extended protocol: parse, sync
        request req(false);
        req.add(protocol::parse_t{.statement_name = "stmt", .query = "SELECT 1", .parameter_type_oids = {}});
        req.add(protocol::sync{});

        BOOST_TEST_EQ(check_request(req), error_code());
    }
}

void test_error()
{
    {
        // Empty request
        request req;

        BOOST_TEST_EQ(check_request(req), error_code(client_errc::empty_request));
    }
}

}  // namespace

int main()
{
    test_success();
    test_error();

    return boost::report_errors();
}
