//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_CLIENT_ERRC_HPP
#define NATIVEPG_CLIENT_ERRC_HPP

#include <boost/system/error_category.hpp>

namespace nativepg {

const boost::system::error_category& get_client_category();

enum class client_errc : int
{
    /// An incomplete message was received from the server (indicates a deserialization error or
    /// packet mismatch).
    incomplete_message = 1,

    /// An unexpected value was found in a server-received message (indicates a deserialization
    /// error or packet mismatch).
    protocol_value_error,

    /// Unexpected extra bytes at the end of a message were received (indicates a deserialization
    /// error or packet mismatch).
    extra_bytes,

    // You passed a collection whose size exceeds a protocol max
    value_too_big,

    // Decoding base64 failed because of malformed input
    invalid_base64,

    // Parsing a SCRAM message failed
    invalid_scram_message,

    // We found a mandatory SCRAM extension ('m'), which requires us to fail
    // authentication in the current version
    mandatory_scram_extension_not_supported,

    // Used in responses to indicate that we want more messages.
    // TODO: this should really be in another category, at least, since it's not an error per se
    needs_more,

    // We got a message type that wasn't supposed to appear in the state we are
    unexpected_message,

    // We got a NULL, but the C++ type where we're parsing into doesn't support NULLs
    unexpected_null,

    // The fields returned by the query are not compatible with the C++ types we're parsing into
    incompatible_field_type,

    // There was a field defined in a C++ type that wasn't present in the data returned by the query
    field_not_found,

    // TODO: this should really use an error category on its own
    auth_failed,

    // We don't support this authentication method yet
    auth_kerberos_v5_unsupported,

    // We don't support this authentication method yet
    auth_cleartext_password_unsupported,

    // We don't support this authentication method yet
    auth_md5_password_unsupported,

    // We don't support this authentication method yet
    auth_gss_unsupported,

    // We don't support this authentication method yet
    auth_sspi_unsupported,

    // We don't support this authentication method yet
    auth_sasl_unsupported,
};

/// Creates an \ref error_code from a \ref client_errc.
inline boost::system::error_code make_error_code(client_errc error)
{
    return boost::system::error_code(static_cast<int>(error), get_client_category());
}

}  // namespace nativepg

namespace boost {
namespace system {

template <>
struct is_error_code_enum<::nativepg::client_errc>
{
    static constexpr bool value = true;
};

}  // namespace system
}  // namespace boost

#endif
