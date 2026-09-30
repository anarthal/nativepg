//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_EXEC_STATE_HPP
#define NATIVEPG_EXEC_STATE_HPP

#include "nativepg/detail/exec_state_impl.hpp"

namespace nativepg {

namespace detail {
struct exec_state_access;
}

// Tracks a single exec operation. Owns whatever the operation holds in the
// connection, and releases it on destruction, so an abandoned operation
// leaves the connection usable.
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

    bool is_prepared() const { return impl_.is_prepared(); }
    bool write_done() const { return impl_.write_done(); }
    bool read_done() const { return impl_.read_done(); }

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
