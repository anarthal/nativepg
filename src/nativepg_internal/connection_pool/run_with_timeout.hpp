//
// Copyright (c) 2019-2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_RUN_WITH_TIMEOUT_HPP
#define NATIVEPG_RUN_WITH_TIMEOUT_HPP

#include <boost/corosio/timeout.hpp>

#include <chrono>

namespace nativepg::detail {

// Runs aw with the specified timeout, or without any timeout at all if dur == 0
template <class Awaitable>
auto run_with_timeout(Awaitable aw, std::chrono::steady_clock::duration dur)
{
    // Most of the time, we do have a timeout, so we optimize for this case.
    // Doing this instead of a conditional is one less frame
    auto deadline = dur.count() > 0 ? std::chrono::steady_clock::now() + dur
                                    : (std::chrono::steady_clock::time_point::max)();
    return boost::corosio::timeout(std::move(aw), deadline);
}

}  // namespace nativepg::detail

#endif
