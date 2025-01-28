//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_PROTOCOL_COMMAND_COMPLETE_HPP
#define NATIVEPG_PROTOCOL_COMMAND_COMPLETE_HPP

#include <boost/core/span.hpp>
#include <boost/system/error_code.hpp>

#include <string_view>

namespace nativepg {
namespace protocol {

struct command_complete
{
    // The command tag. This is usually a single word that identifies which SQL command was completed.
    std::string_view tag;
};
boost::system::error_code parse(boost::span<const unsigned char> data, command_complete& to);

}  // namespace protocol
}  // namespace nativepg

#endif
