//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/asio/buffer.hpp>
#include <boost/system/detail/error_code.hpp>
#include <boost/system/system_error.hpp>
#include <boost/variant2/variant.hpp>

#include <cstddef>
#include <span>

#include "nativepg/client_errc.hpp"
#include "nativepg/protocol/connection_state.hpp"
#include "nativepg/protocol/detail/exec_fsm.hpp"
#include "nativepg/protocol/read_response_fsm.hpp"
#include "nativepg/protocol/startup_fsm.hpp"
#include "nativepg/request.hpp"
#include "nativepg/response_handler.hpp"

using namespace nativepg::protocol;
using boost::system::error_code;
using detail::exec_fsm;
using nativepg::client_errc;

static error_code check_request(const nativepg::request& req)
{
    // Empty requests are not allowed
    if (req.messages().empty())
        return error_code(client_errc::empty_request);

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
            case nativepg::request_message_type::sync: return error_code();
            default:
                return query_seen ? client_errc::request_mixes_simple_advanced_protocols
                                  : client_errc::request_ends_without_sync;
        }
    }

    // There was nothing but queries
    return error_code();
}

exec_fsm::result exec_fsm::resume(
    connection_state& st,
    boost::system::error_code ec,
    std::size_t bytes_transferred
)
{
    if (state_ == state_t::initial)
    {
        // Check that the request is correctly formed
        const request& req = read_fsm_.get_request();
        auto ec = check_request(req);
        if (ec)
            return ec;

        // Perform the response setup
        auto res = read_fsm_.get_handler().setup(req, 0u);
        if (res.ec)
            return res.ec;
        if (res.offset != req.messages().size())
            return result(client_errc::incompatible_response_length);

        state_ = state_t::writing;

        // Write the request
        return result::write(read_fsm_.get_request().payload());
    }

    if (ec)
        return ec;

    // Don't pass the bytes read to the first FSM invocation
    if (state_ == state_t::writing)
    {
        bytes_transferred = 0u;
        state_ = state_t::reading;
    }
    auto act = read_fsm_.resume(st, ec, bytes_transferred);
    switch (act.type())
    {
        case read_response_fsm::result_type::read: return result::read(act.read_buffer());
        case read_response_fsm::result_type::done: return act.error();
        default: BOOST_ASSERT(false); return error_code();
    }
}
