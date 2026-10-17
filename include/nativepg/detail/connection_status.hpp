//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_DETAIL_CONNECTION_STATUS_HPP
#define NATIVEPG_DETAIL_CONNECTION_STATUS_HPP

namespace nativepg::detail {

enum class connection_status
{
    // We haven't ever connected or have shut down correctly
    initial,

    // We are in the process of establishing the connection
    connecting,

    // We are now connected and can run exec(), receive() and friends
    connected,

    // We are now in the process of terminating the connection cleanly
    shutting_down,

    // We got an error. Like initial, but we use it to improve diagnostics
    fatal_error,
};

}  // namespace nativepg::detail

#endif
