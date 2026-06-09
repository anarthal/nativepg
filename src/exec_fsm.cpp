//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/system/error_code.hpp>

#include <cstddef>

#include "nativepg/client_errc.hpp"
#include "nativepg/protocol/connection_state.hpp"
#include "nativepg/protocol/detail/exec_fsm.hpp"
#include "nativepg/protocol/read_response_fsm.hpp"
#include "nativepg/request.hpp"
#include "nativepg/response_handler.hpp"
#include "nativepg_internal/check_request.hpp"

using namespace nativepg::protocol;
using boost::system::error_code;
using detail::exec_fsm;
using nativepg::client_errc;

exec_fsm::result exec_fsm::resume(
    connection_state& st,
    boost::system::error_code ec,
    std::size_t bytes_transferred
)
{
    if (state_ == state_t::initial)
    {
        // Initial checkings
        if (auto ec_req = setup_request(read_fsm_.get_request(), read_fsm_.get_handler()))
            return ec_req;

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
//