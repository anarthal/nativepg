//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_PROTOCOL_EXECUTE_HPP
#define NATIVEPG_PROTOCOL_EXECUTE_HPP

#include <boost/core/span.hpp>
#include <boost/system/error_code.hpp>

#include <cstdint>
#include <string_view>

#include "nativepg/protocol/common.hpp"

namespace nativepg {
namespace protocol {

struct execute
{
    // The name of the portal to execute (an empty string selects the unnamed portal).
    std::string_view portal_name;

    // Maximum number of rows to return, if portal contains a query that returns rows (ignored otherwise).
    // Zero denotes “no limit”.
    std::int32_t max_num_rows;
};
boost::system::error_code serialize(const execute& msg, std::vector<unsigned char>& to);

// May only be sent as a response to execute
struct portal_suspended
{
};
inline boost::system::error_code parse(boost::span<const unsigned char> data, portal_suspended&)
{
    return detail::check_empty(data);
}

}  // namespace protocol
}  // namespace nativepg

#endif
