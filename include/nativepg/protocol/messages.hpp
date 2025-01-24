//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_PROTOCOL_MESSAGES_HPP
#define NATIVEPG_PROTOCOL_MESSAGES_HPP

#include <boost/core/span.hpp>
#include <boost/system/error_code.hpp>
#include <boost/system/result.hpp>
#include <boost/variant2/variant.hpp>

#include <cstdint>

#include "nativepg/protocol/async.hpp"
#include "nativepg/protocol/bind.hpp"
#include "nativepg/protocol/copy.hpp"
#include "nativepg/protocol/describe.hpp"
#include "nativepg/protocol/execution.hpp"
#include "nativepg/protocol/notice_error.hpp"
#include "nativepg/protocol/startup.hpp"

namespace nativepg {
namespace protocol {

//
// Messages that we may receive from the backend
//

// TODO: a) this is variant (encapsulated in result) over variant
// b) a variant with so many possibilities might cause long compile times.
// All options are trivially destructible, make a custom class maybe?
using any_backend_message = boost::variant2::variant<
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
    row_description>;
boost::system::result<any_backend_message> parse(
    std::uint8_t message_type,
    boost::span<const unsigned char> data
);

}  // namespace protocol
}  // namespace nativepg

#endif
