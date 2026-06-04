//
// Copyright (c) 2019-2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_CHECK_POOL_PARAMS_HPP
#define NATIVEPG_CHECK_POOL_PARAMS_HPP

#include <boost/throw_exception.hpp>

#include <stdexcept>

#include "nativepg/co_connection_pool.hpp"

namespace nativepg::detail {

inline void check_pool_params(const pool_params& params)
{
    const char* msg = nullptr;
    if (params.max_size == 0)
        msg = "pool_params::max_size must be greater than zero";
    else if (params.max_size < params.initial_size)
        msg = "pool_params::max_size must be greater than pool_params::initial_size";
    else if (params.connect_timeout.count() < 0)
        msg = "pool_params::connect_timeout must not be negative";
    else if (params.retry_interval.count() <= 0)
        msg = "pool_params::retry_interval must be greater than zero";
    else if (params.ping_interval.count() < 0)
        msg = "pool_params::ping_interval must not be negative";
    else if (params.ping_timeout.count() < 0)
        msg = "pool_params::ping_timeout must not be negative";

    if (msg != nullptr)
    {
        BOOST_THROW_EXCEPTION(std::invalid_argument(msg));
    }
}

}  // namespace nativepg::detail

#endif
