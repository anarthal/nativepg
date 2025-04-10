//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_SRC_BASE64_HPP
#define NATIVEPG_SRC_BASE64_HPP

#include <boost/core/span.hpp>
#include <boost/system/error_code.hpp>

#include <vector>

namespace nativepg {
namespace protocol {
namespace detail {

// Encodes the given input as a base64 string, appending it to the supplied buffer
void base64_encode(boost::span<const unsigned char> input, std::vector<unsigned char>& to);

// Decodes the given input, interpreting it as a base64 string
[[nodiscard]] boost::system::error_code base64_decode(
    boost::span<const unsigned char> input,
    std::vector<unsigned char>& output
);

}  // namespace detail
}  // namespace protocol
}  // namespace nativepg

#endif
