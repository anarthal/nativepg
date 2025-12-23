//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/asio/async_result.hpp>
#include <boost/asio/deferred.hpp>
#include <boost/asio/use_future.hpp>
#include <boost/core/lightweight_test.hpp>

#include <type_traits>

#include "nativepg/detail/disposition.hpp"  // disposition traits for extended_error, should always be included
#include "nativepg/extended_error.hpp"
#include "nativepg/protocol/notice_error.hpp"

using namespace nativepg;
namespace asio = boost::asio;

namespace {

// An async function that completes immediately
template <asio::completion_token_for<void(extended_error)> CompletionToken>
auto async_stub(extended_error err, CompletionToken&& token)
{
    return asio::deferred.values(err)(std::forward<CompletionToken>(token));
}

//
void test_success()
{
    static_assert(std::is_same_v<decltype(async_stub({}, asio::use_future).get()), void>);
    async_stub({}, asio::use_future).get();  // doesn't throw
}

}  // namespace

int main()
{
    test_success();

    return boost::report_errors();
}