//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_PROTOCOL_CONNECTION_STATE_HPP
#define NATIVEPG_PROTOCOL_CONNECTION_STATE_HPP

#include <cstdint>
#include <vector>

#include "nativepg/extended_error.hpp"
#include "nativepg/protocol/detail/read_buffer.hpp"

namespace nativepg::protocol {

struct connection_state
{
    // Write buffer for operations that require it (e.g. startup)
    std::vector<unsigned char> write_buffer;

    // Read buffer. TODO: make this configurable
    detail::read_buffer read_buffer{4096};

    // The ID of the process that is managing our connection (aka connection ID)
    std::uint32_t backend_process_id{};

    // A key that can be used for cancellations
    std::uint32_t backend_secret_key{};

    // TODO: this is safe for now, but is there any case where it may not be?
    diagnostics shared_diag;
};

}  // namespace nativepg::protocol

#endif
