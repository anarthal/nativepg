//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_PROTOCOL_COMMAND_COMPLETE_TAG
#define NATIVEPG_PROTOCOL_COMMAND_COMPLETE_TAG

#include <boost/system/error_code.hpp>

#include <cstdint>
#include <optional>
#include <string_view>

namespace nativepg::protocol {

// Parses the tag that the server sends, which contains
// useful info if the command contains certain statements.
// Returns an error if the tag does not conform to the spec:
//   https://www.postgresql.org/docs/current/protocol-message-formats.html#PROTOCOL-MESSAGE-FORMATS-COMMANDCOMPLETE
boost::system::error_code parse_command_complete_tag(
    std::string_view tag,
    std::optional<std::uint64_t>& affected_rows
);

}  // namespace nativepg::protocol

#endif
