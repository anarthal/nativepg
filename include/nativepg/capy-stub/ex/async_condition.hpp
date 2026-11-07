//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BOOST_CAPY_EX_ASYNC_CONDITION_HPP
#define BOOST_CAPY_EX_ASYNC_CONDITION_HPP

#include <boost/capy/concept/executor.hpp>
#include <boost/capy/continuation.hpp>
#include <boost/capy/detail/config.hpp>
#include <boost/capy/detail/intrusive.hpp>
#include <boost/capy/error.hpp>
#include <boost/capy/ex/await_context.hpp>
#include <boost/capy/results.hpp>

#include <atomic>
#include <coroutine>
#include <new>
#include <stop_token>
#include <utility>

// TODO: this is not currently part of Capy, but should be proposed in the future.
// TODO: this is unreviewed LLM code. Once we agree on direction, it needs a lot of trimming.

/*  async_condition implementation notes
    =======================================

    Same cancellation pattern as async_mutex and async_event (see
    async_mutex for the full discussion on claimed_, stop_cb lifetime,
    member ordering, and threading assumptions).

    Two differences from async_event.

    No state
    --------
    There is no set_ flag. await_ready always returns false, so every
    wait() suspends, and a notify that finds no waiter does nothing at
    all. That is the classic condition variable contract: notifications
    are not remembered. It is why the caller must own the predicate and
    test it before waiting -- see the class docs.

    notify_one forwards, notify_all does not
    ----------------------------------------
    notify_one claims one waiter and posts it. Between the post and the
    resumption the frame can be torn down (abandonment, executor
    shutdown, a chain unwound from its owner). The notification would
    then be lost while the state that prompted it remains, and the
    surviving waiters sleep on work that is ready. So a waiter that was
    claimed by notify_one and is destroyed before it resumes re-issues
    notify_one from its destructor, handing the wakeup to the next
    waiter.

    This is the same resolution tokio reached in drop_notified, that
    event-listener documents on Drop, and that CPython added to
    asyncio.Condition.wait in gh-112202. All three forward a one-waiter
    notification and none forward a broadcast, because a broadcast that
    reaches nobody has lost nothing: every waiter that existed when it
    ran was already woken. notify_all therefore does not set one_.

    Waiters claimed by notify_one are parked in notified_ rather than
    unlinked, so ~async_condition can reach them and clear their back
    pointer. Otherwise a waiter outliving the condition variable would
    forward into freed memory. notify_all keeps async_event's local
    claimed list instead: it has nothing to forward, and a local list
    survives a post that destroys the condition variable itself.

    Open question for review
    ------------------------
    A forwarding destructor can re-enter notify_one while an outer
    notify_one is still unwinding, if the executor runs the post inline
    (ex/immediate.hpp). The recursion is bounded by the waiter count and
    each level handles a distinct waiter, so it is correct, but
    async_mutex chose to flatten the equivalent case with its unlocking_
    scope guard. Worth deciding whether to do the same here.
*/

namespace boost {
namespace capy {

/** Queues coroutines in `wait()` and resumes one or all of them on notification.

    A condition variable lets one coroutine tell others that shared
    state may have changed. Waiters suspend in `wait()` and form a FIFO
    queue; `notify_one()` resumes the longest-waiting one, and
    `notify_all()` resumes every waiter queued at the moment it runs.

    @par No Stored State

    The condition variable holds no flag. A notification delivered when
    no coroutine is waiting is discarded, not remembered, so a waiter
    that arrives afterwards still suspends. This is the contract of
    `std::condition_variable`, `java.util.concurrent.locks.Condition`
    and `asyncio.Condition`, and it is the reason the predicate belongs
    to the caller rather than to this class. Use @ref async_event when
    you want a notification to latch, or @ref async_waker when a single
    coroutine needs a wakeup that cannot be missed.

    @par Why No Mutex

    Every threaded condition variable is bound to a mutex, because the
    predicate reads shared state that another thread may be writing, and
    because without a lock the predicate test and the suspension are two
    separate steps that a writer can slip between -- the classic lost
    wakeup.

    Neither reason applies here. Capy's synchronization primitives are
    single-threaded: they run on one executor, or on a strand over a
    multi-threaded one. No other coroutine runs between the caller's
    predicate test and `await_suspend` linking the waiter into the
    queue, because nothing suspends in between. The sequence is already
    atomic with respect to every other coroutine on that executor, so
    the mutex would protect a window that does not exist.

    What the mutex cannot be dropped without is the predicate loop, and
    that requirement remains -- see below. A lock is still needed around
    state that is shared across executors or threads; use
    @ref async_mutex for that, independently of this class.

    @par Caller Obligations

    Two rules, both consequences of holding no state.

    @li **Do not suspend between testing the predicate and awaiting
        `wait()`.** A `co_await` in that window lets another coroutine
        run, change the state and notify, and the notification is lost
        because this waiter is not queued yet. Test, then await, with
        nothing suspending in between.

    @li **Re-test the predicate after every wakeup.** Resumption is
        posted to the executor rather than run inline, so other
        coroutines run between the notification and the resumption and
        may consume what prompted it. A wakeup means the state may have
        changed, never that it still holds.

    Together these give the familiar loop:

    @code
    while(! ready())
    {
        auto [ec] = co_await cv.wait();
        if(ec)
            break;  // canceled
    }
    @endcode

    @ref wait_until writes that loop for you, at the cost of one
    coroutine frame when it has to block.

    @par Cancellation

    When a coroutine is suspended in `wait()` and its stop token is
    triggered, the waiter completes with `error::canceled` and leaves
    the queue. A canceled waiter is never counted as notified:
    `notify_one()` skips it and wakes the next eligible waiter instead,
    so a cancellation racing a notification cannot swallow it.

    @par Zero Allocation

    No heap allocation occurs for wait or notify operations.

    @par Thread Safety

    Distinct objects: Safe.@n
    Shared objects: Unsafe.

    The operations are designed for single-threaded use on one
    executor. The stop callback may fire from any thread.

    This type is non-copyable and non-movable because suspended waiters
    hold intrusive pointers into the internal lists.

    @see async_event, async_mutex, async_waker, wait_until
*/
class async_condition
{
public:
    class wait_awaiter;

private:
    // Waiters suspended in wait(), in arrival order.
    detail::intrusive_list<wait_awaiter> waiters_;

    // Waiters claimed by notify_one and posted, but not yet resumed.
    // Parked here so the destructor can reach them; see the notes above.
    detail::intrusive_list<wait_awaiter> notified_;

    // Pop waiters until one is claimed, skipping those a stop callback
    // already took. Returns null when no eligible waiter remains.
    wait_awaiter* claim_one() noexcept;

public:
    /** Suspends the caller until a notification arrives, or resumes it with `error::canceled` on a stop
     * request.
     */
    class wait_awaiter : public detail::intrusive_list<wait_awaiter>::node
    {
        friend class async_condition;

        async_condition* cv_;
        continuation cont_;
        executor_ref ex_;

        // Declared before stop_cb_buf_: the callback accesses these
        // members, so they must still be alive if the stop_cb_
        // destructor blocks.
        std::atomic<bool> claimed_{false};
        bool canceled_ = false;
        bool active_ = false;
        bool one_ = false;
        bool resumed_ = false;
        detail::intrusive_list<wait_awaiter>* list_ = nullptr;

        struct cancel_fn
        {
            wait_awaiter* self_;

            void operator()() const noexcept
            {
                if (!self_->claimed_.exchange(true, std::memory_order_acq_rel))
                {
                    self_->canceled_ = true;
                    self_->ex_.post(self_->cont_);
                }
            }
        };

        using stop_cb_t = std::stop_callback<cancel_fn>;

        // Aligned storage for stop_cb_t. Declared last: its destructor
        // may block while the callback accesses the members above.
        BOOST_CAPY_MSVC_WARNING_PUSH
        BOOST_CAPY_MSVC_WARNING_DISABLE(4324)  // padded due to alignas
        alignas(stop_cb_t) unsigned char stop_cb_buf_[sizeof(stop_cb_t)];
        BOOST_CAPY_MSVC_WARNING_POP

        stop_cb_t& stop_cb_() noexcept { return *reinterpret_cast<stop_cb_t*>(stop_cb_buf_); }

    public:
        /** Destroy the awaiter, forwarding an unconsumed notification.

            Destroys the stop callback if one is registered and unlinks
            the awaiter from whichever list holds it, so that neither a
            notification nor the stop callback can reach a destroyed
            awaiter.

            If this waiter had been claimed by `notify_one()` but its
            coroutine was torn down before resuming, the notification
            has not been acted on. Rather than drop it, the destructor
            re-issues `notify_one()` so another waiter receives it. A
            notification from `notify_all()` is not forwarded: every
            waiter queued when it ran was already woken, so nothing is
            owed.
        */
        ~wait_awaiter()
        {
            if (active_)
                stop_cb_().~stop_cb_t();
            if (list_)
            {
                list_->remove(this);
                list_ = nullptr;
            }
            if (one_ && !resumed_ && cv_)
                cv_->notify_one();
        }

        /** Construct an awaiter for the given condition variable.

            @param cv The condition variable to wait on. It must outlive
            the awaiter.
        */
        explicit wait_awaiter(async_condition* cv) noexcept : cv_(cv) {}

        /** Construct by moving.

            The moved-from awaiter is left inert: its destructor no
            longer destroys the stop callback, no longer unlinks, and no
            longer forwards a notification.

            @param o The awaiter to move from.
        */
        wait_awaiter(wait_awaiter&& o) noexcept
            : cv_(o.cv_),
              cont_(o.cont_),
              ex_(o.ex_),
              claimed_(o.claimed_.load(std::memory_order_relaxed)),
              canceled_(o.canceled_),
              active_(std::exchange(o.active_, false)),
              one_(std::exchange(o.one_, false)),
              resumed_(o.resumed_),
              list_(std::exchange(o.list_, nullptr))
        {
        }

        /** Copy construction is disabled; a waiter is linked into the
            wait queue by address.

            @param other The awaiter that would be copied.
        */
        wait_awaiter(wait_awaiter const& other) = delete;

        /** Copy assignment is disabled; a waiter is linked into the
            wait queue by address.

            @param other The awaiter that would be assigned from.

            @return A reference to `*this`.
        */
        wait_awaiter& operator=(wait_awaiter const& other) = delete;

        /** Move assignment is disabled; a waiter is linked into the
            wait queue by address.

            @param other The awaiter that would be moved from.

            @return A reference to `*this`.
        */
        wait_awaiter& operator=(wait_awaiter&& other) = delete;

        /** Report whether the wait can complete without suspending.

            A condition variable stores no state, so there is never a
            notification already in hand.

            @return Always `false`.
        */
        bool await_ready() const noexcept { return false; }

        /** Enqueue the awaiting coroutine until a notification arrives.

            This is the @ref ContextAwaitable overload of
            `await_suspend`.

            If a stop request is already pending on `env->stop_token`,
            the awaiter records the cancellation and does not enqueue.

            Otherwise it stores `h` and `env->executor`, links itself
            into the wait queue, and registers a stop callback on
            `env->stop_token`. Exactly one of a notification and that
            callback posts `h` through the stored executor, whichever
            claims the waiter first. A losing stop callback does
            nothing; a losing notification moves on to the next waiter.

            @param h The awaiting coroutine, resumed on notification or
            cancellation.

            @param env The execution environment. Its executor posts the
            resumption and its stop token is watched for the duration of
            the wait. It must outlive the wait.

            @return `h` if a stop request was already pending, which
            resumes the awaiting coroutine immediately without enqueuing
            it. Otherwise `std::noop_coroutine()`, which leaves the
            coroutine suspended and returns control to the resumer.
        */
        std::coroutine_handle<> await_suspend(std::coroutine_handle<> h, await_context const* env) noexcept
        {
            if (env->stop_token.stop_requested())
            {
                canceled_ = true;
                return h;
            }
            cont_.h = h;
            cont_.owner = env->owner;
            ex_ = env->executor;
            cv_->waiters_.push_back(this);
            list_ = &cv_->waiters_;
            ::new (stop_cb_buf_) stop_cb_t(env->stop_token, cancel_fn{this});
            active_ = true;
            return std::noop_coroutine();
        }

        /** Complete the wait and report the outcome.

            Destroys the stop callback if one is registered and unlinks
            the awaiter from whichever list holds it. Marks the
            notification consumed, so the destructor does not forward
            it.

            @return An empty `results<>` on notification, or one holding
            `error::canceled` if the stop token fired first.
        */
        [[nodiscard]] results<> await_resume() noexcept
        {
            resumed_ = true;
            if (active_)
            {
                stop_cb_().~stop_cb_t();
                active_ = false;
            }
            if (list_)
            {
                list_->remove(this);
                list_ = nullptr;
            }
            if (canceled_)
                return {make_error_code(error::canceled)};
            return {{}};
        }
    };

    /// Construct a condition variable with no waiters.
    async_condition() = default;

    /** Copy construction is disabled; suspended waiters point into the
        internal lists.

        @param other The condition variable that would be copied.
    */
    async_condition(async_condition const& other) = delete;

    /** Copy assignment is disabled; suspended waiters point into the
        internal lists.

        @param other The condition variable that would be assigned from.

        @return A reference to `*this`.
    */
    async_condition& operator=(async_condition const& other) = delete;

    /** Move construction is disabled; suspended waiters point into the
        internal lists.

        @param other The condition variable that would be moved from.
    */
    async_condition(async_condition&& other) = delete;

    /** Move assignment is disabled; suspended waiters point into the
        internal lists.

        @param other The condition variable that would be moved from.

        @return A reference to `*this`.
    */
    async_condition& operator=(async_condition&& other) = delete;

    /** Destroy the condition variable, canceling every coroutine still waiting.

        Each queued waiter is posted to its executor to resume with
        `error::canceled`, as if its stop token had been triggered. An
        executor that can no longer run it abandons it instead, possibly
        before this destructor returns.

        A waiter already notified keeps its notification and still
        resumes successfully; it only loses the ability to forward that
        notification, since there is no longer anything to forward it
        to. Either way no waiter refers to the condition variable
        afterwards.
    */
    ~async_condition()
    {
        while (auto* w = waiters_.pop_front())
        {
            w->list_ = nullptr;
            w->cv_ = nullptr;
            if (!w->claimed_.exchange(true, std::memory_order_acq_rel))
            {
                w->canceled_ = true;
                w->ex_.post(w->cont_);
            }
        }
        while (auto* w = notified_.pop_front())
        {
            w->list_ = nullptr;
            w->cv_ = nullptr;
        }
    }

    /** Returns an awaiter that suspends until a notification arrives.

        The wait always suspends: a condition variable remembers
        nothing, so there is no notification waiting to be consumed.
        Test your predicate before awaiting, and re-test after every
        wakeup; see the class documentation.

        @return An awaitable that await-returns `(error_code)`.
    */
    wait_awaiter wait() noexcept { return wait_awaiter{this}; }

    /** Resumes the longest-waiting coroutine, if any.

        Waiters already claimed by a stop callback are skipped, so the
        notification goes to a waiter that can actually act on it. If no
        eligible waiter remains the call does nothing and the
        notification is discarded.

        If the resumed coroutine is destroyed before it runs, the
        notification is handed to the next waiter rather than lost.

        @note If posting a waiter's resumption throws, `std::terminate`
        is called.
    */
    void notify_one() noexcept
    {
        auto* w = claim_one();
        if (!w)
            return;
        // Park the waiter where the destructor can find it, and do
        // every access to *this before the post: the post can resume
        // the coroutine on the spot and destroy whatever owns this
        // condition variable.
        w->one_ = true;
        notified_.push_back(w);
        w->list_ = &notified_;
        w->ex_.post(w->cont_);
    }

    /** Resumes every coroutine waiting at the moment of the call.

        Waiters already claimed by a stop callback are skipped. A
        coroutine that waits again from its own resumption is not woken
        a second time by this call, because the whole queue is claimed
        before anything is posted.

        A broadcast is never forwarded: a waiter destroyed before it
        resumes simply drops its wakeup, since every waiter queued when
        the call ran was already notified.

        @note If posting a waiter's resumption throws, `std::terminate`
        is called.
    */
    void notify_all() noexcept
    {
        // Claim the whole queue first, then post. The claimed list is a
        // local: a post can destroy a waiter on the spot, and with it
        // whatever owns this condition variable, so nothing below
        // touches *this.
        detail::intrusive_list<wait_awaiter> claimed;
        while (auto* w = waiters_.pop_front())
        {
            w->list_ = nullptr;
            if (!w->claimed_.exchange(true, std::memory_order_acq_rel))
            {
                claimed.push_back(w);
                w->list_ = &claimed;
            }
        }
        while (auto* w = claimed.pop_front())
        {
            w->list_ = nullptr;
            w->ex_.post(w->cont_);
        }
    }
};

inline async_condition::wait_awaiter* async_condition::claim_one() noexcept
{
    while (auto* w = waiters_.pop_front())
    {
        w->list_ = nullptr;
        if (!w->claimed_.exchange(true, std::memory_order_acq_rel))
            return w;
    }
    return nullptr;
}

}  // namespace capy
}  // namespace boost

#endif
