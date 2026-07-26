//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_TEST_CI_SERVER_HPP
#define NATIVEPG_TEST_CI_SERVER_HPP

#include <string>

#include "nativepg/connect_params.hpp"

namespace nativepg::test {

// Retrieves the configured host (environment variable NATIVEPG_SERVER_HOST)
std::string get_host();

inline connect_params default_connect_params()
{
    return {
        .hostname = get_host(),
        .username = "postgres",
        .password = "secret",
        .database = "postgres",
    };
}

}  // namespace nativepg::test

#endif
