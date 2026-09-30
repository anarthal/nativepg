//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_MULTIPLEXER_V2_HPP
#define NATIVEPG_MULTIPLEXER_V2_HPP

#include <boost/capy/buffers.hpp>
#include <boost/capy/buffers/make_buffer.hpp>
#include <boost/capy/ex/async_event.hpp>
#include <boost/capy/ex/async_mutex.hpp>
#include <boost/capy/io/any_stream.hpp>
#include <boost/capy/io_task.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/when_all.hpp>
#include <boost/capy/when_any.hpp>
#include <boost/capy/write.hpp>
#include <boost/intrusive/list.hpp>

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <span>
#include <utility>
#include <vector>

#include "nativepg/detail/exec_state_impl.hpp"
#include "nativepg/request.hpp"

// TODO: impl notes
//   why the write mutex
//   why an intrusive list
//   why pending write bytes
//   why RAII guards

namespace nativepg::detail {

class multiplexer_v2
{
public:
    multiplexer_v2()
    {
        receive_evt_.set();  // tasks start empty
    }

    // TODO: this should have a proper reset to call on connection establishment.
    // If leftovers happen after a connection is severed, they are never cleaned up.

    // Gets a buffer containing leftover bytes from previous execs
    // that should be written before our request
    std::span<const unsigned char> previous_write_bytes() const { return pending_write_; }

    // Reports the result of the writer and releases the guard
    // TODO: this calls potentially-throwing functions and is called from a destructor.
    // An exception here leaves the connection in an unrecoverable state.
    // Exceptions here are rare, so we'll handle this later.
    void report_writer_exit(exec_state_impl& st, std::size_t bytes_written)
    {
        // Did we manage to write any leftover bytes from previous execs?
        if (!pending_write_.empty())
        {
            const std::size_t consumed_bytes = (std::min)(bytes_written, pending_write_.size());
            pending_write_.erase(pending_write_.begin(), pending_write_.begin() + consumed_bytes);
            bytes_written -= consumed_bytes;
        }

        // Did we manage to write any bytes from our request?
        if (bytes_written > 0u)
        {
            // Part of the request has been sent to the server, at least
            st.request_committed = true;

            // If it was only a part, subsequent execs need to send it fully
            // if they want to keep the connection healthy
            if (bytes_written < st.req->payload().size())
            {
                pending_write_.assign(st.req->payload().begin() + bytes_written, st.req->payload().end());
            }
        }

        // The writer is done
        st.writer_st = exec_state_impl::writer_status::done;
        on_writer_exit(st);
    }

    // Waits for our turn to read
    auto wait_for_read(exec_state_impl& st) { return st.evt.wait(); }

    // Reports that the reader has exited with a fatal failure
    void report_reader_exit(exec_state_impl& st)
    {
        st.reader_done = true;
        on_reader_exit(st);
    }

    // Registers a task within the multiplexer and waits for the writer's turn.
    // The state and its request must be kept alive until the state is reset
    boost::capy::io_task<> enter(exec_state_impl& exec_st)
    {
        BOOST_ASSERT(!exec_st.is_linked());

        // Wait for our turn to write
        // TODO: use a guard, as set() may technically throw.
        // scoped_lock() doesn't work because the guard doesn't have a release() method
        auto [ec] = co_await write_mtx_.lock();
        if (ec)
            co_return {ec};

        // We now hold the write mutex and owe an exit report
        exec_st.writer_st = exec_state_impl::writer_status::locked;

        // Register what we are doing, so no other reader takes our turn
        exec_st.pending_rfqs = std::exchange(trailing_rfqs_, 0u);
        active_tasks_.push_back(exec_st);

        // If there is no-one reading, set the event so the reader doesn't deadlock.
        // TODO: I think this needs to go here if we want to move this to the writer
        if (&active_tasks_.front() == &exec_st && !receiver_reading_)
            exec_st.evt.set();

        // Done
        co_return {};
    }

    class receive_guard
    {
        multiplexer_v2* obj_{};

    public:
        receive_guard() = default;
        explicit receive_guard(multiplexer_v2& obj) noexcept : obj_(&obj) {}

        receive_guard(receive_guard&& rhs) noexcept : obj_(std::exchange(rhs.obj_, nullptr)) {}
        receive_guard(const receive_guard& rhs) = delete;
        receive_guard& operator=(receive_guard&& rhs) noexcept
        {
            if (this != &rhs)
            {
                // Release whatever we were holding before taking over rhs's slot
                if (obj_)
                    obj_->on_receiver_exit();
                obj_ = std::exchange(rhs.obj_, nullptr);
            }
            return *this;
        }
        receive_guard& operator=(const receive_guard& rhs) = delete;
        ~receive_guard()
        {
            if (obj_)
                obj_->on_receiver_exit();
        }

        // Is it our turn to read? Or did we get notified because
        // there are new cached notifications?
        bool is_reading() const { return obj_->receiver_reading_; }

        // Returns the number of ReadyForQuery messages that
        // should be read from previous abandoned requests.
        std::size_t previous_rfqs() const { return obj_->trailing_rfqs_; }

        // Reports that we have read a RFQ.
        // If the receiver exits by an exception, we can still know what state the connection is in.
        void report_rfq()
        {
            BOOST_ASSERT(obj_->trailing_rfqs_ > 0u);
            --obj_->trailing_rfqs_;
        }
    };

    // TODO: do we want this as an awaitable instead?
    boost::capy::io_task<receive_guard> enter_receive()
    {
        // Wait for our turn
        if (auto [ec] = co_await receive_evt_.wait(); ec)
            co_return {ec, {}};

        // Reset the event, so further notifications aren't lost
        receive_evt_.clear();

        // This event may be set because there are new cached notifications,
        // or because it's our time to read. Try to distinguish it
        receiver_reading_ = active_tasks_.empty();

        // Done
        co_return {{}, receive_guard{*this}};
    }

    // TODO: this lacks encapsulation
    void notify_receiver() { receive_evt_.set(); }

    // Are there any exec readers waiting?
    bool has_exec_readers() const { return !active_tasks_.empty(); }

private:
    // Grants exclusive access to the write side
    boost::capy::async_mutex write_mtx_;

    // The list of active tasks that need access to the connection
    boost::intrusive::list<exec_state_impl> active_tasks_;

    // RFQs left over by the last task that run
    std::size_t trailing_rfqs_{};

    // Bytes left over by an incomplete write by a previous task
    std::vector<unsigned char> pending_write_;

    // Has the receiver acquired ownership of the reader?
    bool receiver_reading_{};

    // Should be set when there are new notifications
    // or the receiver can attempt to read
    boost::capy::async_event receive_evt_;

    static inline std::size_t count_rfqs(const request& req)
    {
        return std::ranges::count_if(req.messages(), [](request_message_type type) {
            return type == request_message_type::query || type == request_message_type::sync;
        });
    }

    void on_writer_exit(exec_state_impl& st)
    {
        BOOST_ASSERT(write_mtx_.is_locked());
        write_mtx_.unlock();
        if (st.read_done())
            on_both_exited(st);
    }

    void on_reader_exit(exec_state_impl& st)
    {
        if (st.write_done())
            on_both_exited(st);
    }

    void on_both_exited(exec_state_impl& st)
    {
        // Setup
        auto it = active_tasks_.iterator_to(st);
        auto next = std::next(it);
        bool is_current_reader = it == active_tasks_.begin();

        // Compute the remaining RFQs. The reader might set read_rfqs to -1
        // to indicate that everything was read so we can skip this calculation
        // (common case fast)
        // TODO: I think this could technically overflow
        // if many requests are cancelled one after the other
        const std::size_t remaining_rfqs =
            (st.read_rfqs == static_cast<std::size_t>(-1)
                 ? 0u
                 : st.pending_rfqs + (st.request_committed ? count_rfqs(*st.req) : 0u) - st.read_rfqs);

        // Remove ourselves from the list
        active_tasks_.erase(it);

        // Update the leftover RFQ count
        (next == active_tasks_.end() ? trailing_rfqs_ : next->pending_rfqs) += remaining_rfqs;

        // If this is the current reader and there is a next reader, notify it.
        // Otherwise, let the receiver read loop run.
        if (is_current_reader)
            notify_next_reader();
    }

    void on_receiver_exit()
    {
        // If we were reading, we're no longer doing it, so notify any pending readers
        if (receiver_reading_)
            notify_next_reader();
        receiver_reading_ = false;
    }

    void notify_next_reader()
    {
        if (!active_tasks_.empty())
            active_tasks_.front().evt.set();
        else
            receive_evt_.set();
    }
};

// TODO: I think the reader and writer really belong here

}  // namespace nativepg::detail

#endif  // BOOST_REDIS_MULTIPLEXER_HPP
