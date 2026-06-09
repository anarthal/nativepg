//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_SRC_NATIVEPG_INTERNAL_CHECK_REQUEST_HPP
#define NATIVEPG_SRC_NATIVEPG_INTERNAL_CHECK_REQUEST_HPP

#include <boost/system/error_code.hpp>

#include "nativepg/client_errc.hpp"
#include "nativepg/request.hpp"

namespace nativepg::protocol::detail {

// Checks that a request is well-formed, suitable for execution.
// Requests might go through intermediate states when being built,
// but not all of them are suitable for execution.
// To enable appropriate error recovery, requests need to be
// composed of units of either:
//   1. The extended query protocol. This is a set of extended query messages ending with a sync.
//   2. The simple query protocol. This is a single query message.
// Such units may be mixed arbitrarily, but can't be broken down
// (i.e. placing a query in the middle of an extended batch is illegal).
// Doing this, we can manage errors by counting ready_for_query messages.
// TODO: do we need to support requests ending with flush? is there any use case for this?
inline boost::system::error_code check_request(const request& req)
{
    // Empty requests are not allowed
    if (req.messages().empty())
        return client_errc::empty_request;

    bool query_seen = false;
    for (auto it = req.messages().rbegin(); it != req.messages().rend(); ++it)
    {
        switch (*it)
        {
            case nativepg::request_message_type::query: query_seen = true; continue;
            case nativepg::request_message_type::sync: return {};
            default:
                return query_seen ? client_errc::request_mixes_simple_advanced_protocols
                                  : client_errc::request_ends_without_sync;
        }
    }

    // There was nothing but queries
    return {};
}

}  // namespace nativepg::protocol::detail

#endif
