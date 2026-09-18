//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_MULTIPLEXER_V2_HPP
#define NATIVEPG_MULTIPLEXER_V2_HPP

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
#include <boost/intrusive/list_hook.hpp>

#include <cstddef>
#include <iterator>
#include <span>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

#include "nativepg/client_errc.hpp"
#include "nativepg/protocol/any_backend_message.hpp"
#include "nativepg/protocol/connection_state.hpp"
#include "nativepg/protocol/parse_message.hpp"
#include "nativepg/protocol/read_response_fsm.hpp"
#include "nativepg/request.hpp"
#include "nativepg/responses/any_request_message.hpp"
#include "nativepg/responses/response_handler.hpp"
#include "nativepg_internal/multiplexed_connection/multiplexer.hpp"

namespace nativepg::detail {

struct multiplexer_state
{
    static std::size_t count_rfqs(const request& req) { return count_rfqs(req.messages()); }
    static std::size_t count_rfqs(std::span<const request_message_type>);

    struct pending_read : boost::intrusive::list_base_hook<>
    {
        // Number of ReadyForQuery messages that we expect from
        // previously cancelled items
        std::size_t previous_rfqs{};

        // Setting it notifies the task to read next
        boost::capy::async_event evt{};

        // Did the writer write at least one byte of our request?
        bool request_committed{};

        // How many ReadyForQuery messages did the reader read?
        // This includes RFQs from leftover requests before us
        std::size_t read_rfqs{};

        // TODO: we could condense these
        bool writer_exited{}, reader_exited{};

        const request* req;
    };

    // TODO: name wrapping here is bad
    class multiplexer_v2
    {
        boost::capy::async_mutex write_mtx_;
        bool reading_{};
        boost::intrusive::list<pending_read> pending_;
        std::size_t trailing_rfqs_{};

        void on_writer_exit(pending_read& handle)
        {
            BOOST_ASSERT(write_mtx_.is_locked());
            write_mtx_.unlock();
            handle.writer_exited = true;
            if (handle.reader_exited)
                on_both_exited(handle);
        }

        void on_reader_exit(pending_read& handle)
        {
            handle.reader_exited = true;
            if (handle.writer_exited)
                on_both_exited(handle);
        }

        void on_both_exited(pending_read& handle)
        {
            // Compute the remaining RFQs. TODO: we could make this more efficient
            // by not always requiring to compute the number of RFQs
            // TODO: this would require forcing a node in the list even without waiting
            const std::size_t remaining_rfqs = handle.previous_rfqs +
                                               (handle.request_committed ? count_rfqs(handle.req->messages())
                                                                         : 0u) -
                                               handle.read_rfqs;

            if (handle.is_linked())
            {
                // This is a pending reader. Update the next reader's pending RFQ count and exit
                auto it = pending_.iterator_to(handle);
                if (auto next = std::next(it); next == pending_.end())
                {
                    // We're the last reader
                    trailing_rfqs_ += remaining_rfqs;
                }
                else
                {
                    next->previous_rfqs += remaining_rfqs;
                }

                // Remove ourselves from the list
                pending_.erase(it);
            }
            else
            {
                // This is the reader currently running. No need to unlink
                if (pending_.empty())
                {
                    // No more pending reads. Add any remaining ReadyForQuery to the general state
                    trailing_rfqs_ += remaining_rfqs;
                    reading_ = false;
                }
                else
                {
                    // There are pending reads. Add the remaining ReadyForQuery's to the first
                    // reader's state and notify it
                    auto& item = pending_.front();
                    pending_.pop_front();
                    item.previous_rfqs += remaining_rfqs;
                    item.evt.set();
                }
            }
        }

    public:
        multiplexer_v2() = default;

        class write_guard
        {
            multiplexer_v2* obj_{};
            pending_read* handle_{};

        public:
            write_guard() = default;
            explicit write_guard(multiplexer_v2& obj, pending_read& handle) noexcept
                : obj_(&obj), handle_(&handle)
            {
            }

            write_guard(write_guard&& rhs) noexcept
                : obj_(std::exchange(rhs.obj_, nullptr)), handle_(rhs.handle_)
            {
            }
            write_guard(const write_guard& rhs) = delete;
            write_guard& operator=(write_guard&& rhs) noexcept;  // TODO
            write_guard& operator=(const write_guard& rhs) = delete;
            ~write_guard()
            {
                if (obj_)
                    obj_->on_writer_exit(*handle_);
            }

            void report_request_commited() { handle_->request_committed = true; }
        };

        class read_guard
        {
            multiplexer_v2* obj_{};
            pending_read* handle_{};

        public:
            read_guard() = default;
            explicit read_guard(multiplexer_v2& obj, pending_read& handle) noexcept
                : obj_(&obj), handle_(&handle)
            {
            }

            read_guard(read_guard&& rhs) noexcept
                : obj_(std::exchange(rhs.obj_, nullptr)), handle_(rhs.handle_)
            {
            }
            read_guard(const read_guard& rhs) = delete;
            read_guard& operator=(read_guard&& rhs) noexcept;  // TODO
            read_guard& operator=(const read_guard& rhs) = delete;
            ~read_guard()
            {
                if (obj_)
                    obj_->on_reader_exit(*handle_);
            }

            // Waits for our turn.
            auto wait() { return handle_->evt.wait(); }

            // Returns the number of ReadyForQuery messages that
            // should be read from previous abandoned requests.
            // Only meaningful after wait() completes.
            std::size_t previous_rfqs() const { return handle_->previous_rfqs; }

            void report_rfq() { ++handle_->read_rfqs; }
        };

        boost::capy::io_task<write_guard, read_guard> enter(pending_read& handle, const request* req)
        {
            // Wait for our turn to write
            // TODO: use lock_guard here
            if (auto [ec] = co_await write_mtx_.lock(); ec)
                co_return {ec, {}, {}};

            // If there is no-one reading, set the event so the reader doesn't deadlock
            if (pending_.empty())
                handle.evt.set();

            // Register what we are doing, so no other reader takes our turn
            handle.previous_rfqs = std::exchange(trailing_rfqs_, 0u);
            pending_.push_back(handle);
            handle.req = req;

            // Done
            co_return {{}, write_guard(*this, handle), read_guard(*this, handle)};
        }
    };

    multiplexer_v2 mpx_;
    std::vector<unsigned char> pending_writes;  // TODO

    // This is the writer side of exec, and should be called with the mutex acquired
    boost::capy::io_task<> write_request(
        boost::capy::any_stream& stream,
        multiplexer_v2::write_guard guard,
        const request& req
    )
    {
        // If there are any half-written previous requests, write these first.
        // We could use vectored I/O here, but this is a very rare case and optimizing
        // for it is not worth it.
        if (!pending_writes.empty())
        {
            auto [ec, bytes] = co_await boost::capy::write(stream, boost::capy::make_buffer(pending_writes));
            if (ec)
            {
                // There was an error or cancellation
                pending_writes.erase(pending_writes.begin(), pending_writes.begin() + bytes);
                co_return {ec};
            }

            // We wrote what was remaining of the previous message
            pending_writes.clear();
        }

        // Now, our own message
        auto [ec, bytes] = co_await boost::capy::write(stream, boost::capy::make_buffer(req.payload()));

        // We have sent at least one byte and thus committed to running the request
        // if we want to keep the connection healthy
        if (bytes > 0u)
            guard.report_request_commited();

        // There was a short write, probably due to an error or cancellation.
        // We need to store what we didn't write so the connection doesn't break
        // TODO: this cleanup should likely be in the guard
        if (bytes < req.payload().size())
            pending_writes.assign(req.payload().begin() + bytes, req.payload().end());

        // Done
        co_return {ec};
    }

    boost::capy::io_task<> read_some_messages(
        boost::capy::any_stream& stream,
        protocol::connection_state& st
    );

    boost::capy::io_task<> read_response(
        boost::capy::any_stream& stream,
        protocol::connection_state& st,
        multiplexer_v2::read_guard guard,
        const request& req,
        response_handler_ref handler
    )
    {
        // Wait for our turn
        if (auto [ec] = co_await guard.wait(); ec)
            co_return {ec};

        // Setup
        protocol::read_response_fsm fsm{&req, handler, false};  // disallow COPY
        std::size_t consumed = 0u;
        std::size_t remaining_prev_rfqs = guard.previous_rfqs();

        while (true)
        {
            // Try to parse a cached message
            auto bytes = st.read_buffer.committed_area();
            auto res = protocol::parse_message(bytes.subspan(consumed));

            // Check for errors and end of input.
            // Errors here are irrecoverable.
            if (res.ec)
            {
                st.read_buffer.consume(consumed);
                consumed = 0u;
                if (res.ec == client_errc::needs_more)
                {
                    if (auto [ec] = co_await read_some_messages(stream, st); ec)
                        co_return {ec};
                    continue;
                }
                else
                {
                    co_return {res.ec};
                }
            }

            // We have a message
            consumed += res.size;
            st.update_tracked(res.message);
            bool is_rfq = res.message.type() == protocol::any_backend_message::kind::ready_for_query;
            if (is_rfq)
                guard.report_rfq();

            // Act on the message
            if (remaining_prev_rfqs > 0u)
            {
                // A leftover message from previous execs
                if (is_rfq)
                    --remaining_prev_rfqs;
            }
            else
            {
                // One of our messages
                auto fsm_ec = fsm.resume(res.message);
                if (!fsm_ec)
                {
                    // We've finished successfully
                    st.read_buffer.consume(consumed);
                    co_return {fsm.get_handler_error().code};  // TODO: diagnostics?
                }
                else if (fsm_ec != client_errc::needs_more)
                {
                    // There has been a severe protocol violation (unrecoverable)
                    st.read_buffer.consume(consumed);
                    co_return {fsm_ec};
                }
            }
        }
    }

    boost::capy::io_task<> exec(
        boost::capy::any_stream& stream,
        protocol::connection_state& st,
        const request& req,
        response_handler_ref handler
    )
    {
        // Wait for our turn to write and register what we are doing in the queue
        pending_read handle;
        auto [enter_ec, write_guard, read_guard] = co_await mpx_.enter(handle, &req);
        if (enter_ec)
            co_return {enter_ec};

        // Run the reader and writer tasks in parallel
        auto [final_ec, writer_dummy, reader_dummy] = co_await boost::capy::when_all(
            write_request(stream, std::move(write_guard), req),
            read_response(stream, st, std::move(read_guard), req, handler)
        );

        co_return {final_ec};
    }
};

}  // namespace nativepg::detail

#endif  // BOOST_REDIS_MULTIPLEXER_HPP
