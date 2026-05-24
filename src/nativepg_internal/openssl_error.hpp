//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_SRC_NATIVEPG_INTERNAL_OPENSSL_ERROR_HPP
#define NATIVEPG_SRC_NATIVEPG_INTERNAL_OPENSSL_ERROR_HPP

#include <boost/system/error_code.hpp>

namespace nativepg::detail {

// Translates an OpenSSL error code (as returned by ERR_get_error) into a
// boost::system::error_code, using Asio's SSL error category.
[[nodiscard]] boost::system::error_code translate_openssl_error(unsigned long code);

}  // namespace nativepg::detail

#endif
