//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/capy/ex/this_coro.hpp>
#include <boost/capy/task.hpp>
#include <boost/core/lightweight_test.hpp>

#include <ostream>
#include <string_view>

#include "nativepg/co_connection.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/protocol/async.hpp"
#include "nativepg/request.hpp"
#include "test_utils/co_connection_utils.hpp"
#include "test_utils/corosio_utils.hpp"
#include "test_utils/printing.hpp"
#include "test_utils/test_range_eq.hpp"

namespace capy = boost::capy;
using namespace nativepg;
using namespace nativepg::test;
using namespace std::string_view_literals;

namespace nativepg::protocol {

std::ostream& operator<<(std::ostream& os, const notification_response& value)
{
    return os << "{ .process_id=" << value.process_id << ", .channel_name=" << value.channel_name
              << ", .payload=" << value.payload << " }";
}

}  // namespace nativepg::protocol

namespace {

// A notification raised by another connection reaches a receive() that runs
// alone on an established connection
capy::task<> test_single_notification()
{
    // Setup
    auto conn = co_await establish_connection(), notifier = co_await establish_connection();

    // Listen
    if (!co_await checked_exec(conn, request().add_query("LISTEN \"test_receive_single\"")))
        co_return;

    // Raise the notification
    if (!co_await checked_exec(notifier, request().add_query("NOTIFY test_receive_single, 'some payload'")))
        co_return;

    // Receive
    auto [ec, notifs] = co_await conn.receive();
    if (!check_success(ec, {}))
        co_return;

    // Check
    const protocol::notification_response expected[] = {
        {.process_id = notifier.state().backend_process_id,
         .channel_name = "test_receive_single",
         .payload = "some payload"}
    };
    test_range_eq(notifs, expected);
}

// Empty payloads don't cause trouble
capy::task<> test_empty_payload()
{
    // Setup
    auto conn = co_await establish_connection(), notifier = co_await establish_connection();

    // Listen
    if (!co_await checked_exec(conn, request().add_query("LISTEN \"test_empty_payload\"")))
        co_return;

    // Raise the notification
    if (!co_await checked_exec(notifier, request().add_query("NOTIFY test_empty_payload")))
        co_return;

    // Receive
    auto [ec, notifs] = co_await conn.receive();
    if (!check_success(ec, {}))
        co_return;

    // Check
    const protocol::notification_response expected[] = {
        {.process_id = notifier.state().backend_process_id,
         .channel_name = "test_empty_payload",
         .payload = ""}
    };
    test_range_eq(notifs, expected);
}

}  // namespace

int main()
{
    run_coroutine_test(test_single_notification());
    run_coroutine_test(test_empty_payload());

    return boost::report_errors();
}
