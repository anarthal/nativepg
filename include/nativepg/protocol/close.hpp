//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_PROTOCOL_CLOSE_HPP
#define NATIVEPG_PROTOCOL_CLOSE_HPP

#include <boost/system/error_code.hpp>

#include <vector>

#include "nativepg/protocol/common.hpp"

namespace nativepg {
namespace protocol {

// Requests to close a prepared statement or portal
struct close
{
    static constexpr unsigned char message_type = static_cast<unsigned char>('C');

    // Whether to close a prepared statement or a portal
    portal_or_statement type;

    // The name of the prepared statement or portal to close (an empty string selects the unnamed prepared
    // statement or portal).
    std::string_view name;
};
boost::system::error_code serialize(const close& msg, std::vector<unsigned char>& to);

// Response
struct close_complete
{
};
inline boost::system::error_code parse(boost::span<const unsigned char> data, close_complete&)
{
    return detail::check_empty(data);
}

}  // namespace protocol
}  // namespace nativepg

#endif
