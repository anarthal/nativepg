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

inline boost::system::error_code check_request(const request& req)
{
    // Empty requests are not allowed
    if (req.messages().empty())
        return client_errc::empty_request;

    // To enable appropriate error recovery, requests need to either
    //   1. End with a sync
    //   2. Be a sequence of query messages, with nothing in front of it
    //   3. End with a sequence of query messages, with a sync in front of it
    // TODO: do we need to support requests ending with flush? is there any use case for this?
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
