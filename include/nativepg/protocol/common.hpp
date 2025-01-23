//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_PROTOCOL_COMMON_HPP
#define NATIVEPG_PROTOCOL_COMMON_HPP

#include <boost/core/span.hpp>
#include <boost/system/error_code.hpp>

namespace nativepg {
namespace protocol {

namespace detail {

boost::system::error_code check_empty(boost::span<const unsigned char> data);

}

enum class format_code : std::int16_t
{
    text = 0,
    binary = 1,
};

}  // namespace protocol
}  // namespace nativepg

#endif
