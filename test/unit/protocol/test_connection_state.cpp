//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/assert/source_location.hpp>
#include <boost/core/lightweight_test.hpp>

#include <iostream>
#include <optional>
#include <ostream>

#include "nativepg/encoding.hpp"
#include "nativepg/protocol/any_backend_message.hpp"
#include "nativepg/protocol/connection_state.hpp"
#include "test_utils/printing.hpp"
#include "test_utils/test_opt_eq.hpp"

using namespace nativepg;
using namespace nativepg::protocol;
using namespace nativepg::test;

namespace {

// Comprehensive list of what we track
struct tracked_values
{
    std::uint32_t backend_process_id;
    std::uint32_t backend_secret_key;
    std::optional<bool> standard_conforming_strings;
    std::optional<encoding> client_encoding;

    explicit tracked_values(const connection_state& st) noexcept
        : backend_process_id(st.backend_process_id),
          backend_secret_key(st.backend_secret_key),
          standard_conforming_strings(st.standard_conforming_strings),
          client_encoding(st.client_encoding)
    {
    }

    friend bool operator==(const tracked_values&, const tracked_values&) noexcept = default;
    friend std::ostream& operator<<(std::ostream& os, const tracked_values& v)
    {
        return os << "{ .backend_process_id=" << v.backend_process_id
                  << ", .backend_secret_key=" << v.backend_secret_key
                  << ", .standard_conforming_strings=" << optional_wrapper{v.standard_conforming_strings}
                  << ", .client_encoding=" << optional_wrapper{v.client_encoding} << " }";
    }
};

connection_state make_initial_state()
{
    return connection_state{
        .backend_process_id = 42u,
        .backend_secret_key = 43u,
        .standard_conforming_strings = true,
        .client_encoding = encoding::latin1,
    };
}

// Checks that a message leaves every tracked field untouched
void check_no_change(const any_backend_message& msg, boost::source_location loc = BOOST_CURRENT_LOCATION)
{
    auto st = make_initial_state();
    tracked_values expected{st};

    st.update_tracked(msg);

    if (!BOOST_TEST_EQ(tracked_values{st}, expected))
        std::cerr << "  Called from " << loc << std::endl;
}

// A name we know about updates client_encoding
void test_client_encoding_known()
{
    auto st = make_initial_state();
    tracked_values expected{st};

    st.update_tracked(parameter_status{.name = "client_encoding", .value = "UTF8"});

    expected.client_encoding = encoding::utf8;
    BOOST_TEST_EQ(tracked_values{st}, expected);
}

// A name we don't know about makes client_encoding unknown
void test_client_encoding_unknown()
{
    auto st = make_initial_state();
    tracked_values expected{st};

    st.update_tracked(parameter_status{.name = "client_encoding", .value = "NONSENSE"});

    expected.client_encoding.reset();
    BOOST_TEST_EQ(tracked_values{st}, expected);
}

// Known standard_conforming_strings values update the tracked value
void test_standard_conforming_strings_on()
{
    auto st = make_initial_state();
    st.standard_conforming_strings.reset();  // make the value change
    tracked_values expected{st};

    st.update_tracked(parameter_status{.name = "standard_conforming_strings", .value = "on"});

    expected.standard_conforming_strings = true;
    BOOST_TEST_EQ(tracked_values{st}, expected);
}

void test_standard_conforming_strings_off()
{
    auto st = make_initial_state();
    tracked_values expected{st};

    st.update_tracked(parameter_status{.name = "standard_conforming_strings", .value = "off"});

    expected.standard_conforming_strings = false;
    BOOST_TEST_EQ(tracked_values{st}, expected);
}

// Anything other than on/off makes the value unknown
void test_standard_conforming_strings_other()
{
    auto st = make_initial_state();
    tracked_values expected{st};

    st.update_tracked(parameter_status{.name = "standard_conforming_strings", .value = "maybe"});

    expected.standard_conforming_strings.reset();
    BOOST_TEST_EQ(tracked_values{st}, expected);
}

// BackendKeyData updates the cancellation data, and nothing else
void test_backend_key_data()
{
    auto st = make_initial_state();
    tracked_values expected{st};

    st.update_tracked(backend_key_data{.process_id = 0x1234, .secret_key = 0x5678});

    expected.backend_process_id = 0x1234u;
    expected.backend_secret_key = 0x5678u;
    BOOST_TEST_EQ(tracked_values{st}, expected);
}

// Parameters we don't track are ignored
void test_other_parameter_names()
{
    // Parameter names actually sent by Postgres
    check_no_change(parameter_status{.name = "application_name", .value = "myapp"});
    check_no_change(parameter_status{.name = "DateStyle", .value = "ISO, MDY"});
    check_no_change(parameter_status{.name = "server_encoding", .value = "UTF8"});
    check_no_change(parameter_status{.name = "is_superuser", .value = "on"});

    // A prefix of a name we track is still a different name
    check_no_change(parameter_status{.name = "client_encod", .value = "UTF8"});

    // An empty name/value is OK
    check_no_change(parameter_status{.name = "", .value = ""});
    check_no_change(parameter_status{.name = "", .value = "UTF8"});
}

// Every other message type leaves the state alone
void test_other_messages()
{
    check_no_change(any_backend_message{});
    check_no_change(authentication_ok{});
    check_no_change(authentication_kerberos_v5{});
    check_no_change(authentication_cleartext_password{});
    check_no_change(authentication_md5_password{});
    check_no_change(authentication_gss{});
    check_no_change(authentication_gss_continue{});
    check_no_change(authentication_sspi{});
    check_no_change(authentication_sasl{});
    check_no_change(authentication_sasl_continue{});
    check_no_change(authentication_sasl_final{});
    check_no_change(bind_complete{});
    check_no_change(close_complete{});
    check_no_change(command_complete{});
    check_no_change(copy_data{});
    check_no_change(copy_done{});
    check_no_change(copy_fail{});
    check_no_change(copy_in_response{});
    check_no_change(copy_out_response{});
    check_no_change(copy_both_response{});
    check_no_change(data_row{});
    check_no_change(empty_query_response{});
    check_no_change(error_response{});
    check_no_change(negotiate_protocol_version{});
    check_no_change(no_data{});
    check_no_change(notice_response{});
    check_no_change(notification_response{});
    check_no_change(parameter_description{});
    check_no_change(parse_complete{});
    check_no_change(portal_suspended{});
    check_no_change(ready_for_query{});
    check_no_change(field_description{});
    check_no_change(row_description{});
}

}  // namespace

int main()
{
    test_client_encoding_known();
    test_client_encoding_unknown();

    test_standard_conforming_strings_on();
    test_standard_conforming_strings_off();
    test_standard_conforming_strings_other();

    test_backend_key_data();

    test_other_parameter_names();
    test_other_messages();

    return boost::report_errors();
}
