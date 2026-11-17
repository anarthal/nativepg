//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_CONTEXT_HPP
#define NATIVEPG_CONTEXT_HPP


#include <memory>

#include "nativepg/registries/types_registry.hpp"

namespace nativepg {
class connection;
class co_connection;

class context
{
    std::unique_ptr<types_registry> types_;

    friend class connection;
    friend class co_connection;

public:

    const std::unique_ptr<types_registry>& types() const    {
        return types_;
    }
};

}  // namespace nativepg

#endif // NATIVEPG_CONTEXT_HPP
