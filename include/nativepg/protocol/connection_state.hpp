//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_PROTOCOL_CONNECTION_STATE_HPP
#define NATIVEPG_PROTOCOL_CONNECTION_STATE_HPP

#include <cstdint>
#include <optional>
#include <vector>

#include "nativepg/encoding.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/protocol/detail/read_buffer.hpp"

namespace nativepg::protocol {

class any_backend_message;

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

    // GUCs reported via ParameterStatus
    std::optional<bool> standard_conforming_strings;
    std::optional<encoding> client_encoding;

    // TODO: this is safe for now, but is there any case where it may not be?
    diagnostics shared_diag;

    // Updates GUCs as required
    void update_tracked(const any_backend_message& msg);

    void reset_gucs()
    {
        standard_conforming_strings.reset();
        client_encoding.reset();
    }

    void reset()
    {
        write_buffer.clear();
        read_buffer.reset();
        backend_process_id = {};
        backend_secret_key = {};
        reset_gucs();
        // shared_diag are transient by nature
    }
};

}  // namespace nativepg::protocol

#endif
