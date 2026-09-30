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
    enum class writer_status
    {
        initial,
        locked,
        done,
    };

    detail::multiplexer_v2* mpx{};
    writer_status writer_st{writer_status::initial};
    bool request_registered{};
    bool reader_done{};
    detail::multiplexer_v2::task_node node;
    diagnostics* diag{};
    std::optional<protocol::read_response_fsm> fsm;  // TODO: optional not good

    void reset();
};

struct exec_state_access;

}  // namespace detail

class exec_state
{
    detail::exec_state_impl impl_;

    friend struct detail::exec_state_access;

public:
    exec_state() = default;
    exec_state(const exec_state&) = delete;
    exec_state(exec_state&&) = delete;
    exec_state& operator=(const exec_state&) = delete;
    exec_state& operator=(exec_state&&) = delete;
    ~exec_state() { impl_.reset(); }

    bool is_registered() const { return impl_.mpx != nullptr; }
    bool write_done() const { return impl_.writer_st == detail::exec_state_impl::writer_status::done; }
    bool read_done() const { return impl_.reader_done; }

    void reset() { impl_.reset(); }
};

namespace detail {

struct exec_state_access
{
    static exec_state_impl& get_impl(exec_state& st) { return st.impl_; }
};

}  // namespace detail

}  // namespace nativepg

#endif
