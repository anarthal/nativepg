//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_PROTOCOL_ANY_BACKEND_MESSAGE_HPP
#define NATIVEPG_PROTOCOL_ANY_BACKEND_MESSAGE_HPP

#include <boost/core/span.hpp>
#include <boost/system/result.hpp>

#include <cstdint>

#include "nativepg/protocol/async.hpp"
#include "nativepg/protocol/bind.hpp"
#include "nativepg/protocol/close.hpp"
#include "nativepg/protocol/command_complete.hpp"
#include "nativepg/protocol/copy.hpp"
#include "nativepg/protocol/data_row.hpp"
#include "nativepg/protocol/describe.hpp"
#include "nativepg/protocol/empty_query_response.hpp"
#include "nativepg/protocol/execute.hpp"
#include "nativepg/protocol/notice_error.hpp"
#include "nativepg/protocol/parse.hpp"
#include "nativepg/protocol/ready_for_query.hpp"
#include "nativepg/protocol/startup.hpp"

namespace nativepg {
namespace protocol {

// Contains any of the messages that we may receive from the backend.
// Hand-rolled variant-like type.
// All getters require checking kind() first
class any_backend_message
{
public:
    enum class kind
    {
        none,
        authentication_ok,
        authentication_kerberos_v5,
        authentication_cleartext_password,
        authentication_md5_password,
        authentication_gss,
        authentication_gss_continue,
        authentication_sspi,
        authentication_sasl,
        authentication_sasl_continue,
        authentication_sasl_final,
        backend_key_data,
        bind_complete,
        close_complete,
        command_complete,
        copy_data,
        copy_done,
        copy_fail,
        copy_in_response,
        copy_out_response,
        copy_both_response,
        data_row,
        empty_query_response,
        error_response,
        negotiate_protocol_version,
        no_data,
        notice_response,
        notification_response,
        parameter_description,
        parameter_status,
        parse_complete,
        portal_suspended,
        ready_for_query,
        field_description,
        row_description,
    };

    // Default constructor: constructs an invalid message
    any_backend_message() noexcept : kind_(kind::none), none_{} {}

    // Constructors from types (intentionally non-explicit)
    any_backend_message(const authentication_ok& v) noexcept
        : kind_(kind::authentication_ok), authentication_ok_(v)
    {
    }
    any_backend_message(const authentication_kerberos_v5& v) noexcept
        : kind_(kind::authentication_kerberos_v5), authentication_kerberos_v5_(v)
    {
    }
    any_backend_message(const authentication_cleartext_password& v) noexcept
        : kind_(kind::authentication_cleartext_password), authentication_cleartext_password_(v)
    {
    }
    any_backend_message(const authentication_md5_password& v) noexcept
        : kind_(kind::authentication_md5_password), authentication_md5_password_(v)
    {
    }
    any_backend_message(const authentication_gss& v) noexcept
        : kind_(kind::authentication_gss), authentication_gss_(v)
    {
    }
    any_backend_message(const authentication_gss_continue& v) noexcept
        : kind_(kind::authentication_gss_continue), authentication_gss_continue_(v)
    {
    }
    any_backend_message(const authentication_sspi& v) noexcept
        : kind_(kind::authentication_sspi), authentication_sspi_(v)
    {
    }
    any_backend_message(const authentication_sasl& v) noexcept
        : kind_(kind::authentication_sasl), authentication_sasl_(v)
    {
    }
    any_backend_message(const authentication_sasl_continue& v) noexcept
        : kind_(kind::authentication_sasl_continue), authentication_sasl_continue_(v)
    {
    }
    any_backend_message(const authentication_sasl_final& v) noexcept
        : kind_(kind::authentication_sasl_final), authentication_sasl_final_(v)
    {
    }
    any_backend_message(const backend_key_data& v) noexcept
        : kind_(kind::backend_key_data), backend_key_data_(v)
    {
    }
    any_backend_message(const bind_complete& v) noexcept : kind_(kind::bind_complete), bind_complete_(v) {}
    any_backend_message(const close_complete& v) noexcept : kind_(kind::close_complete), close_complete_(v) {}
    any_backend_message(const command_complete& v) noexcept
        : kind_(kind::command_complete), command_complete_(v)
    {
    }
    any_backend_message(const copy_data& v) noexcept : kind_(kind::copy_data), copy_data_(v) {}
    any_backend_message(const copy_done& v) noexcept : kind_(kind::copy_done), copy_done_(v) {}
    any_backend_message(const copy_fail& v) noexcept : kind_(kind::copy_fail), copy_fail_(v) {}
    any_backend_message(const copy_in_response& v) noexcept
        : kind_(kind::copy_in_response), copy_in_response_(v)
    {
    }
    any_backend_message(const copy_out_response& v) noexcept
        : kind_(kind::copy_out_response), copy_out_response_(v)
    {
    }
    any_backend_message(const copy_both_response& v) noexcept
        : kind_(kind::copy_both_response), copy_both_response_(v)
    {
    }
    any_backend_message(const data_row& v) noexcept : kind_(kind::data_row), data_row_(v) {}
    any_backend_message(const empty_query_response& v) noexcept
        : kind_(kind::empty_query_response), empty_query_response_(v)
    {
    }
    any_backend_message(const error_response& v) noexcept : kind_(kind::error_response), error_response_(v) {}
    any_backend_message(const negotiate_protocol_version& v) noexcept
        : kind_(kind::negotiate_protocol_version), negotiate_protocol_version_(v)
    {
    }
    any_backend_message(const no_data& v) noexcept : kind_(kind::no_data), no_data_(v) {}
    any_backend_message(const notice_response& v) noexcept : kind_(kind::notice_response), notice_response_(v)
    {
    }
    any_backend_message(const notification_response& v) noexcept
        : kind_(kind::notification_response), notification_response_(v)
    {
    }
    any_backend_message(const parameter_description& v) noexcept
        : kind_(kind::parameter_description), parameter_description_(v)
    {
    }
    any_backend_message(const parameter_status& v) noexcept
        : kind_(kind::parameter_status), parameter_status_(v)
    {
    }
    any_backend_message(const parse_complete& v) noexcept : kind_(kind::parse_complete), parse_complete_(v) {}
    any_backend_message(const portal_suspended& v) noexcept
        : kind_(kind::portal_suspended), portal_suspended_(v)
    {
    }
    any_backend_message(const ready_for_query& v) noexcept : kind_(kind::ready_for_query), ready_for_query_(v)
    {
    }
    any_backend_message(const field_description& v) noexcept
        : kind_(kind::field_description), field_description_(v)
    {
    }
    any_backend_message(const row_description& v) noexcept : kind_(kind::row_description), row_description_(v)
    {
    }

    // Gets the kind
    kind type() const noexcept { return kind_; }

    const authentication_ok& get_authentication_ok() const noexcept { return authentication_ok_; }
    const authentication_kerberos_v5& get_authentication_kerberos_v5() const noexcept
    {
        return authentication_kerberos_v5_;
    }
    const authentication_cleartext_password& get_authentication_cleartext_password() const noexcept
    {
        return authentication_cleartext_password_;
    }
    const authentication_md5_password& get_authentication_md5_password() const noexcept
    {
        return authentication_md5_password_;
    }
    const authentication_gss& get_authentication_gss() const noexcept { return authentication_gss_; }
    const authentication_gss_continue& get_authentication_gss_continue() const noexcept
    {
        return authentication_gss_continue_;
    }
    const authentication_sspi& get_authentication_sspi() const noexcept { return authentication_sspi_; }
    const authentication_sasl& get_authentication_sasl() const noexcept { return authentication_sasl_; }
    const authentication_sasl_continue& get_authentication_sasl_continue() const noexcept
    {
        return authentication_sasl_continue_;
    }
    const authentication_sasl_final& get_authentication_sasl_final() const noexcept
    {
        return authentication_sasl_final_;
    }
    const backend_key_data& get_backend_key_data() const noexcept { return backend_key_data_; }
    const bind_complete& get_bind_complete() const noexcept { return bind_complete_; }
    const close_complete& get_close_complete() const noexcept { return close_complete_; }
    const command_complete& get_command_complete() const noexcept { return command_complete_; }
    const copy_data& get_copy_data() const noexcept { return copy_data_; }
    const copy_done& get_copy_done() const noexcept { return copy_done_; }
    const copy_fail& get_copy_fail() const noexcept { return copy_fail_; }
    const copy_in_response& get_copy_in_response() const noexcept { return copy_in_response_; }
    const copy_out_response& get_copy_out_response() const noexcept { return copy_out_response_; }
    const copy_both_response& get_copy_both_response() const noexcept { return copy_both_response_; }
    const data_row& get_data_row() const noexcept { return data_row_; }
    const empty_query_response& get_empty_query_response() const noexcept { return empty_query_response_; }
    const error_response& get_error_response() const noexcept { return error_response_; }
    const negotiate_protocol_version& get_negotiate_protocol_version() const noexcept
    {
        return negotiate_protocol_version_;
    }
    const no_data& get_no_data() const noexcept { return no_data_; }
    const notice_response& get_notice_response() const noexcept { return notice_response_; }
    const notification_response& get_notification_response() const noexcept { return notification_response_; }
    const parameter_description& get_parameter_description() const noexcept { return parameter_description_; }
    const parameter_status& get_parameter_status() const noexcept { return parameter_status_; }
    const parse_complete& get_parse_complete() const noexcept { return parse_complete_; }
    const portal_suspended& get_portal_suspended() const noexcept { return portal_suspended_; }
    const ready_for_query& get_ready_for_query() const noexcept { return ready_for_query_; }
    const field_description& get_field_description() const noexcept { return field_description_; }
    const row_description& get_row_description() const noexcept { return row_description_; }

private:
    kind kind_;
    union
    {
        unsigned char none_;
        authentication_ok authentication_ok_;
        authentication_kerberos_v5 authentication_kerberos_v5_;
        authentication_cleartext_password authentication_cleartext_password_;
        authentication_md5_password authentication_md5_password_;
        authentication_gss authentication_gss_;
        authentication_gss_continue authentication_gss_continue_;
        authentication_sspi authentication_sspi_;
        authentication_sasl authentication_sasl_;
        authentication_sasl_continue authentication_sasl_continue_;
        authentication_sasl_final authentication_sasl_final_;
        backend_key_data backend_key_data_;
        bind_complete bind_complete_;
        close_complete close_complete_;
        command_complete command_complete_;
        copy_data copy_data_;
        copy_done copy_done_;
        copy_fail copy_fail_;
        copy_in_response copy_in_response_;
        copy_out_response copy_out_response_;
        copy_both_response copy_both_response_;
        data_row data_row_;
        empty_query_response empty_query_response_;
        error_response error_response_;
        negotiate_protocol_version negotiate_protocol_version_;
        no_data no_data_;
        notice_response notice_response_;
        notification_response notification_response_;
        parameter_description parameter_description_;
        parameter_status parameter_status_;
        parse_complete parse_complete_;
        portal_suspended portal_suspended_;
        ready_for_query ready_for_query_;
        field_description field_description_;
        row_description row_description_;
    };
};

boost::system::result<any_backend_message> parse(
    std::uint8_t message_type,
    boost::span<const unsigned char> data
);

}  // namespace protocol
}  // namespace nativepg

#endif
