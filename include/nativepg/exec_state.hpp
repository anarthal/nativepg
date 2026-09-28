//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_EXEC_STATE_HPP
#define NATIVEPG_EXEC_STATE_HPP

#include <optional>

#include "nativepg/detail/multiplexer_v2.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/protocol/read_response_fsm.hpp"

namespace nativepg {

// TODO: move visibility
// TODO: hide this
struct exec_state
{
    detail::multiplexer_v2::task_node node{};
    detail::multiplexer_v2::write_guard write_guard;
    detail::multiplexer_v2::read_guard read_guard;
    diagnostics* diag{};
    std::optional<protocol::read_response_fsm> fsm;  // TODO: optional not good
};

}  // namespace nativepg

#endif
