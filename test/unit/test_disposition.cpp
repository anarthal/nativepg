//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/asio/async_result.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/deferred.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/use_future.hpp>
#include <boost/core/lightweight_test.hpp>
#include <boost/system/system_error.hpp>

#include <exception>
#include <type_traits>

#include "nativepg/client_errc.hpp"
#include "nativepg/detail/disposition.hpp"  // disposition traits for extended_error, should always be included (TODO: ODR violation prone)
#include "nativepg/extended_error.hpp"
#include "nativepg/protocol/notice_error.hpp"

using namespace nativepg;
using boost::system::error_code;
namespace asio = boost::asio;

namespace {

// An async function that completes immediately
template <asio::completion_token_for<void(extended_error)> CompletionToken>
auto async_stub(extended_error err, CompletionToken&& token)
{
    return asio::deferred.values(err)(std::forward<CompletionToken>(token));
}

// Asio recognizes our type as an error type.
static_assert(std::is_same_v<decltype(async_stub({}, asio::use_future).get()), void>);

// If our function completes with success, no error is thrown
void test_success()
{
    async_stub({}, asio::use_future).get();  // doesn't throw
}

// If our function completes with an error, it's translated to an exception.
// use_future needs to store the exception, so it exercises the functions
// that convert an error to an exception_ptr
void test_error_exception_ptr()
{
    protocol::error_response msg{
        {.severity = "ERROR", .sqlstate = "42P01", .message = "relation does not exist"}
    };
    auto fut = async_stub({.code = client_errc::value_too_big, .diag = msg}, asio::use_future);

    try
    {
        fut.get();
        BOOST_TEST(false);
    }
    catch (const boost::system::system_error& err)
    {
        BOOST_TEST_EQ(err.code(), error_code(client_errc::value_too_big));
        BOOST_TEST_EQ(
            std::string_view(err.what()),
            "ERROR: 42P01: relation does not exist: <unknown nativepg client error> [nativepg.client:4]"
        );  // TODO: update when we implement proper client_errc to string conversion
    }
}

// Same, but for a completion token that uses throw_exception
void test_error_throw_exception()
{
    asio::io_context ctx;
    std::exception_ptr exc;
    extended_error err{
        .code = client_errc::value_too_big,
        .diag = protocol::error_response{
            {.severity = "ERROR", .sqlstate = "42P01", .message = "relation does not exist"}
        }
    };

    asio::co_spawn(ctx, async_stub(err, asio::use_awaitable), [&exc](std::exception_ptr ptr) {
        exc = std::move(ptr);
    });

    ctx.run();

    try
    {
        BOOST_TEST(exc != nullptr);
        std::rethrow_exception(exc);
        BOOST_TEST(false);
    }
    catch (const boost::system::system_error& err)
    {
        BOOST_TEST_EQ(err.code(), error_code(client_errc::value_too_big));
        BOOST_TEST_EQ(
            std::string_view(err.what()),
            "ERROR: 42P01: relation does not exist: <unknown nativepg client error> [nativepg.client:4]"
        );  // TODO: update when we implement proper client_errc to string conversion
    }
}

}  // namespace

int main()
{
    test_success();
    test_error_exception_ptr();
    test_error_throw_exception();

    return boost::report_errors();
}