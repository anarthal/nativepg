//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_CONTEXT_HPP
#define NATIVEPG_CONTEXT_HPP


#include "nativepg/types/registry.hpp"

namespace nativepg {

class connection_context
{
    // owns the registry_
    std::unique_ptr<types::type_registry> registry_;

public:
    // connection_context is single source of truth per connection and may not be copied or moved
    connection_context() = default;
    ~connection_context() = default;

    connection_context(const connection_context&) = delete;
    connection_context(connection_context&&) = delete;

    connection_context& operator=(const connection_context&) = delete;
    connection_context& operator=(connection_context&&) = delete;

    [[nodiscard]] const types::type_registry& types() const { return *registry_; }
    types::type_registry& types(types::type_registry& new_reg) { registry_ = std::make_unique<types::type_registry>(new_reg); return *registry_; }
};

}  // namespace nativepg

#endif  // NATIVEPG_CONTEXT_HPP
