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

namespace detail {

struct exec_state_impl
{
    detail::multiplexer_v2::task_node node;
    detail::multiplexer_v2::write_guard write_guard;
    detail::multiplexer_v2::read_guard read_guard;
    diagnostics* diag{};
    std::optional<protocol::read_response_fsm> fsm;  // TODO: optional not good
};

struct exec_state_access;

}  // namespace detail

class exec_state
{
    detail::exec_state_impl impl_;

    friend struct detail::exec_state_access;

public:
    exec_state() = default;
    bool is_registered() const { return impl_.fsm.has_value(); }
    bool write_done() const { return is_registered() && !impl_.write_guard.has_value(); }
    bool read_done() const { return is_registered() && !impl_.read_guard.has_value(); }
};

namespace detail {

struct exec_state_access
{
    static exec_state_impl& get_impl(exec_state& st) { return st.impl_; }
};

}  // namespace detail

}  // namespace nativepg

#endif
