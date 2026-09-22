//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_NOTIFICATION_STORE_HPP
#define NATIVEPG_NOTIFICATION_STORE_HPP

#include <span>

#include "nativepg/protocol/async.hpp"

namespace nativepg::detail {

class notification_store
{
public:
    notification_store() = default;

    void push_shallow(const protocol::notification_response&);
    void push_deep(const protocol::notification_response&);
    void clear();

    // TODO: this won't be span, but similar
    std::span<const protocol::notification_response> get() const;
};

}  // namespace nativepg::detail

#endif  // BOOST_REDIS_MULTIPLEXER_HPP
