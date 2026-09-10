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
#include <string_view>
#include <vector>

#include "nativepg/encoding.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/protocol/any_backend_message.hpp"
#include "nativepg/protocol/async.hpp"
#include "nativepg/protocol/detail/read_buffer.hpp"
#include "nativepg/protocol/parse_encoding.hpp"

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

    // GUCs reported via ParameterStatus
    std::optional<bool> standard_conforming_strings;
    std::optional<encoding> client_encoding;

    // TODO: this is safe for now, but is there any case where it may not be?
    diagnostics shared_diag;

    // TODO: move to compiled
    void update_tracked(const any_backend_message& msg)
    {
        switch (msg.type())
        {
            case any_backend_message::kind::backend_key_data:
                update_tracked(msg.get_backend_key_data());
                return;
            case any_backend_message::kind::parameter_status:
                update_tracked(msg.get_parameter_status());
                return;
            default: return;
        }
    }

    void update_tracked(const backend_key_data& msg)
    {
        backend_process_id = msg.process_id;
        backend_secret_key = msg.secret_key;
    }

    void update_tracked(const parameter_status& msg)
    {
        // Any value we don't understand leaves the GUC as unknown, rather than
        // guessing: escaping with the wrong assumption is a correctness problem.
        if (msg.name == "standard_conforming_strings")
        {
            if (msg.value == "on")
                standard_conforming_strings = true;
            else if (msg.value == "off")
                standard_conforming_strings = false;
            else
                standard_conforming_strings.reset();
        }
        else if (msg.name == "client_encoding")
        {
            client_encoding = parse_encoding(msg.value);
        }
    }

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
