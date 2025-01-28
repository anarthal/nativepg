//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_PROTOCOL_EMPTY_QUERY_RESPONSE_HPP
#define NATIVEPG_PROTOCOL_EMPTY_QUERY_RESPONSE_HPP

#include <boost/core/span.hpp>
#include <boost/system/error_code.hpp>

#include "nativepg/protocol/common.hpp"

namespace nativepg {
namespace protocol {

struct empty_query_response
{
};
inline boost::system::error_code parse(boost::span<const unsigned char> data, empty_query_response&)
{
    return detail::check_empty(data);
}

}  // namespace protocol
}  // namespace nativepg

#endif
