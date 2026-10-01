//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_TEST_YIELD_HPP
#define NATIVEPG_TEST_YIELD_HPP

#include <boost/capy/concept/io_awaitable.hpp>
#include <boost/capy/continuation.hpp>
#include <boost/capy/ex/io_env.hpp>

#include <coroutine>

namespace nativepg::test {

// An awaitable that reschedules the awaiting coroutine on its own executor,
// giving anything else that is ready a chance to run first.
// Useful to make tests deterministic when they need another task to reach a
// certain point before continuing.
// Never fails, and can't be cancelled.
class yield
{
    boost::capy::continuation cont_{};

public:
    yield() = default;

    // Always suspends: completing inline would defeat the purpose
    bool await_ready() const noexcept { return false; }

    std::coroutine_handle<> await_suspend(std::coroutine_handle<> h, const boost::capy::io_env* env)
    {
        cont_.h = h;
        env->executor.post(cont_);
        return std::noop_coroutine();
    }

    void await_resume() const noexcept {}
};

}  // namespace nativepg::test

#endif
