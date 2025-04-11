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
