//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_PROTOCOL_DETAIL_EXEC_FSM_HPP
#define NATIVEPG_PROTOCOL_DETAIL_EXEC_FSM_HPP

#include <boost/system/error_code.hpp>

#include <cstddef>

#include "nativepg/extended_error.hpp"
#include "nativepg/protocol/connection_state.hpp"
#include "nativepg/protocol/read_response_fsm.hpp"
#include "nativepg/protocol/startup_fsm.hpp"
#include "nativepg/request.hpp"
#include "nativepg/response_handler.hpp"

namespace nativepg::protocol::detail {

// TODO: test
class exec_fsm
{
public:
    // TODO: this is not good
    using result_type = startup_fsm::result_type;
    using result = startup_fsm::result;

    exec_fsm(const request& req, response_handler_ref handler) noexcept : read_fsm_(req, handler) {}

    const request& get_request() const { return read_fsm_.get_request(); }

    result resume(connection_state& st, boost::system::error_code ec, std::size_t bytes_transferred);

    extended_error get_result(boost::system::error_code ec) const
    {
        return ec ? extended_error{ec, {}} : read_fsm_.get_handler().result();
    }

private:
    enum class state_t
    {
        initial,
        writing,
        reading
    };

    state_t state_{state_t::initial};
    read_response_fsm read_fsm_;
};

}  // namespace nativepg::protocol::detail

#endif
