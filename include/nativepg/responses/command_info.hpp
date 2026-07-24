//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_COMMAND_INFO_HPP
#define NATIVEPG_COMMAND_INFO_HPP

#include <cstdint>
#include <optional>
#include <string>

namespace nativepg {

// Information about the execution of a command included in the
// CommandComplete / PortalSuspended
struct command_info
{
    // The tag in the CommandComplete message. Akin to PQcmdStatus
    std::string command_complete_tag{};

    // The rows affected by the command. Only available for a subset of commands.
    // Shouldn't be taken for granted. Akin to PQcmdTuples
    std::optional<std::uint64_t> affected_rows{};

    // True if the max row count specified in an execute message was reached.
    bool portal_suspended{false};

    friend bool operator==(const command_info&, const command_info&) = default;
};

}  // namespace nativepg

#endif
