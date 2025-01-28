//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_PROTOCOL_READY_FOR_QUERY_HPP
#define NATIVEPG_PROTOCOL_READY_FOR_QUERY_HPP

#include <boost/core/span.hpp>
#include <boost/system/error_code.hpp>

namespace nativepg {
namespace protocol {

// Sent when operations complete
enum class transaction_status : unsigned char
{
    // not in a transaction block
    idle = 'I',

    // in a transaction block
    in_transaction = 'T',

    // in a failed transaction block (queries will be rejected until block is ended)
    failed = 'E',
};

struct ready_for_query
{
    // Current backend transaction status indicator
    transaction_status status;
};
boost::system::error_code parse(boost::span<const unsigned char> data, ready_for_query& to);

}  // namespace protocol
}  // namespace nativepg

#endif
