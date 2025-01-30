//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_PROTOCOL_SYNC_HPP
#define NATIVEPG_PROTOCOL_SYNC_HPP

#include <boost/system/error_code.hpp>

namespace nativepg {
namespace protocol {

struct sync
{
};
boost::system::error_code serialize(sync, std::vector<unsigned char>& to);

}  // namespace protocol
}  // namespace nativepg

#endif
