//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_WRITE_STATUS_HPP
#define NATIVEPG_WRITE_STATUS_HPP

namespace nativepg {

// What should the writer side of an exec operation do next?
// Check doc/write_status.svg for the possible state transitions.
enum class write_status
{
    // The request hasn't been written in full yet. Call write_request().
    request,

    // The writer can't make progress until the reader does.
    // Only reachable when using exclusivity::exclusive.
    // Call read_some_response().
    waiting_for_reader,

    // The server is expecting copy data from us. Call write_some_copy_data(),
    // write_copy_done() or write_copy_fail().
    copy_data,

    // The writer has nothing left to do. Terminal.
    done,
};

}  // namespace nativepg

#endif
