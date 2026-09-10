//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/assert/source_location.hpp>
#include <boost/core/lightweight_test.hpp>

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
using kind = any_backend_message::kind;

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

// Values that make an unexpected change visible: any test that expects
// no change checks that these survive untouched.
constexpr std::uint32_t initial_process_id = 42u;
constexpr std::uint32_t initial_secret_key = 43u;

void set_initial_values(connection_state& st)
{
    st.backend_process_id = initial_process_id;
    st.backend_secret_key = initial_secret_key;
    st.update_tracked(parameter_status{.name = "client_encoding", .value = "LATIN1"});
    st.update_tracked(parameter_status{.name = "standard_conforming_strings", .value = "on"});
}

connection_state make_initial_state()
{
    return connection_state{
        .backend_process_id = initial_process_id,
        .backend_secret_key = initial_secret_key,
        .standard_conforming_strings = true,
        .client_encoding = encoding::latin1,
    };
}

// Checks that a message leaves every tracked field untouched
void check_no_change(const any_backend_message& msg, boost::source_location loc = BOOST_CURRENT_LOCATION)
{
    connection_state st;
    set_initial_values(st);

    st.update_tracked(msg);

    test_opt_eq(st.client_encoding, encoding::latin1, loc);
    test_opt_eq(st.standard_conforming_strings, true, loc);
    BOOST_TEST_EQ(st.backend_process_id, initial_process_id);
    BOOST_TEST_EQ(st.backend_secret_key, initial_secret_key);
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

// A name we don't know about makes client_encoding unknown, rather than
// leaving the previous (now wrong) value in place
void test_client_encoding_unknown()
{
    connection_state st;
    set_initial_values(st);

    st.update_tracked(parameter_status{.name = "client_encoding", .value = "NONSENSE"});

    test_opt_eq(st.client_encoding, std::nullopt);
    test_opt_eq(st.standard_conforming_strings, true);
}

void test_standard_conforming_strings_on()
{
    connection_state st;
    set_initial_values(st);
    st.update_tracked(parameter_status{.name = "standard_conforming_strings", .value = "off"});

    st.update_tracked(parameter_status{.name = "standard_conforming_strings", .value = "on"});

    test_opt_eq(st.standard_conforming_strings, true);
    test_opt_eq(st.client_encoding, encoding::latin1);
}

void test_standard_conforming_strings_off()
{
    connection_state st;
    set_initial_values(st);

    st.update_tracked(parameter_status{.name = "standard_conforming_strings", .value = "off"});

    test_opt_eq(st.standard_conforming_strings, false);
    test_opt_eq(st.client_encoding, encoding::latin1);
}

// Anything other than on/off makes the value unknown
void test_standard_conforming_strings_other()
{
    connection_state st;
    set_initial_values(st);

    st.update_tracked(parameter_status{.name = "standard_conforming_strings", .value = "maybe"});

    test_opt_eq(st.standard_conforming_strings, std::nullopt);
    test_opt_eq(st.client_encoding, encoding::latin1);
}

// Parameters we don't track are ignored
void test_other_keys()
{
    check_no_change(parameter_status{.name = "application_name", .value = "myapp"});
    check_no_change(parameter_status{.name = "DateStyle", .value = "ISO, MDY"});
    check_no_change(parameter_status{.name = "server_encoding", .value = "UTF8"});
    check_no_change(parameter_status{.name = "is_superuser", .value = "on"});

    // A prefix of a name we track is still a different name
    check_no_change(parameter_status{.name = "client_encod", .value = "UTF8"});
}

void test_empty_key()
{
    check_no_change(parameter_status{.name = "", .value = ""});
    check_no_change(parameter_status{.name = "", .value = "UTF8"});
}

// BackendKeyData updates the cancellation data, and nothing else
void test_backend_key_data()
{
    connection_state st;
    set_initial_values(st);

    st.update_tracked(backend_key_data{.process_id = 0x1234, .secret_key = 0x5678});

    BOOST_TEST_EQ(st.backend_process_id, 0x1234u);
    BOOST_TEST_EQ(st.backend_secret_key, 0x5678u);
    test_opt_eq(st.client_encoding, encoding::latin1);
    test_opt_eq(st.standard_conforming_strings, true);
}

// Compile-time guard: adding a message kind breaks this switch (-Werror=switch),
// as a reminder to extend test_other_messages() below.
void check_kinds_exhaustive(kind k)
{
    switch (k)
    {
        case kind::none:
        case kind::authentication_ok:
        case kind::authentication_kerberos_v5:
        case kind::authentication_cleartext_password:
        case kind::authentication_md5_password:
        case kind::authentication_gss:
        case kind::authentication_gss_continue:
        case kind::authentication_sspi:
        case kind::authentication_sasl:
        case kind::authentication_sasl_continue:
        case kind::authentication_sasl_final:
        case kind::backend_key_data:
        case kind::bind_complete:
        case kind::close_complete:
        case kind::command_complete:
        case kind::copy_data:
        case kind::copy_done:
        case kind::copy_fail:
        case kind::copy_in_response:
        case kind::copy_out_response:
        case kind::copy_both_response:
        case kind::data_row:
        case kind::empty_query_response:
        case kind::error_response:
        case kind::negotiate_protocol_version:
        case kind::no_data:
        case kind::notice_response:
        case kind::notification_response:
        case kind::parameter_description:
        case kind::parameter_status:
        case kind::parse_complete:
        case kind::portal_suspended:
        case kind::ready_for_query:
        case kind::field_description:
        case kind::row_description: return;
    }
}

// Every other message type leaves the state alone
void test_other_messages()
{
    check_kinds_exhaustive(kind::none);

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
    test_other_keys();
    test_empty_key();
    test_backend_key_data();
    test_other_messages();

    return boost::report_errors();
}
