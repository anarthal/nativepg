//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_CONNECT_PARAMS_HPP
#define NATIVEPG_CONNECT_PARAMS_HPP

#include <string>

namespace nativepg {

struct connect_params
{
    // TODO: physical params
    std::string username;
    std::string password;
    std::string database;
    // TODO: support arbitrary startup params?
};

}  // namespace nativepg

#endif
