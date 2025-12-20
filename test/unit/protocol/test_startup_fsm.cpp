//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/core/lightweight_test.hpp>
#include <boost/system/error_code.hpp>

#include <iterator>

#include "nativepg/protocol/async.hpp"
#include "nativepg/protocol/connection_state.hpp"
#include "nativepg/protocol/ready_for_query.hpp"
#include "nativepg/protocol/startup.hpp"
#include "nativepg/protocol/startup_fsm.hpp"

using namespace nativepg;
using boost::system::error_code;
using protocol::detail::startup_fsm_impl;

// Operators
static const char* to_string(startup_fsm_impl::result_type t)
{
    switch (t)
    {
        case startup_fsm_impl::result_type::done: return "done";
        case startup_fsm_impl::result_type::read: return "error";
        case startup_fsm_impl::result_type::write: return "message";
        default: return "<unknown startup_fsm_impl::result_type>";
    }
}

namespace nativepg::protocol::detail {

std::ostream& operator<<(std::ostream& os, startup_fsm_impl::result_type t) { return os << to_string(t); }

}  // namespace nativepg::protocol::detail

namespace {

void test_success()
{
    protocol::startup_params params{.username = "postgres", .password = "", .database = "postgres"};
    protocol::connection_state st;
    startup_fsm_impl fsm{params};

    // Initiate. The FSM asks us to write the initial message
    auto res = fsm.resume(st);
    BOOST_TEST_EQ(res.type, startup_fsm_impl::result_type::write);
    const unsigned char expected_msg[] = {
        0x00, 0x00, 0x00, 0x29, 0x00, 0x03, 0x00, 0x00, 0x75, 0x73, 0x65, 0x72, 0x00, 0x70,
        0x6f, 0x73, 0x74, 0x67, 0x72, 0x65, 0x73, 0x00, 0x64, 0x61, 0x74, 0x61, 0x62, 0x61,
        0x73, 0x65, 0x00, 0x70, 0x6f, 0x73, 0x74, 0x67, 0x72, 0x65, 0x73, 0x00, 0x00,
    };
    BOOST_TEST_ALL_EQ(
        st.write_buffer.begin(),
        st.write_buffer.end(),
        std::begin(expected_msg),
        std::end(expected_msg)
    );

    // Write successful
    res = fsm.resume(st);
    BOOST_TEST_EQ(res.type, startup_fsm_impl::result_type::read);

    // Server sends us an authentication success message. We still need to wait until ReadyForQuery
    res = fsm.resume(st, protocol::authentication_ok{});
    BOOST_TEST_EQ(res.type, startup_fsm_impl::result_type::read);

    // Server sends us some parameter status messages, which are currently discarded
    res = fsm.resume(st, protocol::parameter_status{.name = "client_encoding", .value = "utf8"});
    BOOST_TEST_EQ(res.type, startup_fsm_impl::result_type::read);
    res = fsm.resume(st, protocol::parameter_status{.name = "in_hot_standby", .value = "off"});
    BOOST_TEST_EQ(res.type, startup_fsm_impl::result_type::read);

    // Server sends us the identifiers for cancellation
    res = fsm.resume(st, protocol::backend_key_data{.process_id = 10, .secret_key = 42});
    BOOST_TEST_EQ(st.backend_process_id, 10);
    BOOST_TEST_EQ(st.backend_secret_key, 42);
    BOOST_TEST_EQ(res.type, startup_fsm_impl::result_type::read);

    // Server sends us ReadyForQuery
    res = fsm.resume(st, protocol::ready_for_query{.status = protocol::transaction_status::idle});
    BOOST_TEST_EQ(res.type, startup_fsm_impl::result_type::done);
    BOOST_TEST_EQ(res.ec, error_code());
}

// TODO: this needs much more testing once we have a more stable API

}  // namespace

int main()
{
    test_success();

    return boost::report_errors();
}
