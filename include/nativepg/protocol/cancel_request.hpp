//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_PROTOCOL_CANCEL_REQUEST_HPP
#define NATIVEPG_PROTOCOL_CANCEL_REQUEST_HPP

#include <boost/system/error_code.hpp>

#include <cstdint>
#include <vector>

namespace nativepg {
namespace protocol {

struct cancel_request
{
    // The process ID of the target backend.
    std::int32_t process_id;

    // The secret key for the target backend.
    std::int32_t secret_key;
};
boost::system::error_code serialize(const cancel_request& msg, std::vector<unsigned char>& to);

}  // namespace protocol
}  // namespace nativepg

#endif
