//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_PROTOCOL_QUERY_HPP
#define NATIVEPG_PROTOCOL_QUERY_HPP

#include <boost/system/error_code.hpp>

#include <string_view>
#include <vector>

namespace nativepg {
namespace protocol {

struct query
{
    // The query string itself.
    // TODO: allow for generators and vector I/O
    std::string_view query;
};
boost::system::error_code serialize(query, std::vector<unsigned char>& to);

}  // namespace protocol
}  // namespace nativepg

#endif
