//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_PROTOCOL_CONNECTION_STATE_HPP
#define NATIVEPG_PROTOCOL_CONNECTION_STATE_HPP

#include <boost/beast/core/flat_buffer.hpp>
#include <boost/system/error_code.hpp>

#include <cstdint>
#include <vector>

namespace nativepg::protocol {

struct connection_state
{
    // TODO: I think we should be able to merge this with the read buffer if careful. Separate is easier for
    // now
    std::vector<unsigned char> write_buffer;

    // TODO: don't depend on Beast only for this
    // Read buffer
    boost::beast::flat_buffer read_buffer;

    // The ID of the process that is managing our connection (aka connection ID)
    std::uint32_t backend_process_id{};

    // A key that can be used for cancellations
    std::uint32_t backend_secret_key{};
};

}  // namespace nativepg::protocol

#endif
