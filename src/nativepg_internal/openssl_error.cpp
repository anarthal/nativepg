//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/asio/ssl/error.hpp>
#include <boost/system/error_code.hpp>

#include <openssl/err.h>
#include <openssl/opensslv.h>

#include "nativepg/client_errc.hpp"
#include "nativepg_internal/openssl_error.hpp"

namespace nativepg::detail {

// TODO: I'd prefer having our own OpenSSL category
boost::system::error_code translate_openssl_error(unsigned long code)
{
    using boost::system::error_code;

    // If ERR_SYSTEM_ERROR is true, the error code is a system error.
    // This macro only exists since OpenSSL 3
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
    if (ERR_SYSTEM_ERROR(code))
        return error_code(ERR_GET_REASON(code), boost::system::system_category());
#endif

    // In OpenSSL < 3, error codes > 0x80000000 are reserved for the user,
    // so it's unlikely that we will encounter these here. Overflow here
    // is implementation-defined behavior (and not UB), so we're fine.
    // This is what Asio does, anyway.
    int int_code = static_cast<int>(code);

    // An error code of zero would mean success, while this function is always
    // called because an OpenSSL primitive failed. It might indicate that OpenSSL
    // did not provide any extra error information. But it should still be an error
    if (int_code == 0)
        return error_code(client_errc::unknown_openssl_error);
    return error_code(int_code, boost::asio::error::get_ssl_category());
}

}  // namespace nativepg::detail
