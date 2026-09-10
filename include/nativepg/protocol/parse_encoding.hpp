//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_PROTOCOL_PARSE_ENCODING_HPP
#define NATIVEPG_PROTOCOL_PARSE_ENCODING_HPP

#include <optional>
#include <string_view>

#include "nativepg/encoding.hpp"

namespace nativepg::protocol {

// Parses a client_encoding value, as reported by the server in a ParameterStatus message.
// Returns an empty optional if the name is not one of the encodings we know about.
std::optional<encoding> parse_encoding(std::string_view name);

}  // namespace nativepg::protocol

#endif
