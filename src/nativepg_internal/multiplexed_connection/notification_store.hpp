//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_NOTIFICATION_STORE_HPP
#define NATIVEPG_NOTIFICATION_STORE_HPP

#include <boost/assert.hpp>

#include <span>

#include "nativepg/protocol/async.hpp"

namespace nativepg::detail {

// Two modes, deep (full copy) and shallow (shallow copies)
class notification_store
{
public:
    // Constructed as deep by default
    notification_store() = default;

    bool is_deep() const;

    // Precondition: container empty
    void set_deep(bool deep);
    void push_back(const protocol::notification_response& notif);
    void clear();

    std::span<const protocol::notification_response> get() const;
};

}  // namespace nativepg::detail

#endif
