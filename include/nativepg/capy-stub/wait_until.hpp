//
// Copyright (c) 2026 Ruben Perez
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_WAIT_UNTIL_HPP
#define BOOST_CAPY_WAIT_UNTIL_HPP

#include <boost/capy/detail/config.hpp>
#include "nativepg/capy-stub/ex/async_condition.hpp"
#include <boost/capy/results.hpp>
#include <boost/capy/task.hpp>

#include <concepts>
#include <system_error>

// TODO: this is not currently part of Capy, but should be proposed in the future.
// TODO: this is unreviewed LLM code. Once we agree on direction, it needs a lot of trimming.

namespace boost {
namespace capy {

/** Wait on a condition variable until a predicate holds.

    @par Await-effects

    Evaluates `pred()`. While it returns `false`, awaits `cv.wait()` and
    evaluates it again on each wakeup, until either the predicate holds
    or the wait reports a contingency.

    If `pred()` is already `true` no awaiting of `cv.wait()` is
    performed. This is not a contingency.

    @par Await-returns
    An object of type `results<>` destructuring as `[ec]`.

    Contingencies:

    @li The first contingency reported from awaiting `cv.wait()`. The
        predicate is not evaluated again afterwards, so it may still be
        `false` on return.

    Notable conditions:

    @li @c cond::canceled — Operation was canceled.

    @par Await-postcondition
    If `ec` is success then `pred()` returned `true`; otherwise the
    predicate is unsatisfied and the wait was canceled.

    @par Allocation

    This is a coroutine, so a call that has to block allocates one
    frame from the environment's frame allocator. Await
    @ref async_condition::wait directly and write the loop by hand
    where that matters; unlike the primitives in `boost/capy/ex`, this
    algorithm makes no zero-allocation guarantee.

    @par Rationale

    @ref async_condition stores no state, so a wakeup means only that
    the state may have changed. Re-testing the predicate after every
    wakeup is therefore mandatory, and a loop written by hand is easy to
    get wrong. This function is that loop, written once. It mirrors the
    split between `asyncio.Condition.wait` and
    `asyncio.Condition.wait_for`.

    The predicate is evaluated only on this coroutine, never inside
    `notify_one()` or `notify_all()`, so it may read whatever state the
    caller's executor owns without reentrancy concerns. It must not
    suspend.

    @param cv The condition variable to wait on. If its lifetime ends
    before the coroutine finishes, the behavior is undefined. Destroying
    it cancels the wait, so the coroutine does finish.

    @param pred The predicate to evaluate. It is invoked on the
    coroutine's executor and must not suspend or throw.

    @return A task yielding `results<>`.

    @par Remarks
    Supports _ContextAwaitable cancellation_.

    @see async_condition
*/
template<class Pred>
    requires std::predicate<Pred&>
auto
wait_until(async_condition& cv, Pred pred) ->
        task<results<>>
{
    while(! pred())
    {
        auto [ec] = co_await cv.wait();
        if(ec)
            co_return {ec};
    }
    co_return {std::error_code()};
}

} // namespace capy
} // namespace boost

#endif
