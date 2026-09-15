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
#include <boost/capy/when_all.hpp>
#include <boost/capy/write.hpp>
#include <boost/intrusive/list.hpp>
#include <boost/intrusive/list_hook.hpp>

#include <cstddef>
#include <deque>
#include <iterator>
#include <utility>
#include <vector>

#include "nativepg/protocol/views.hpp"
#include "nativepg/request.hpp"
#include "nativepg/responses/response_handler.hpp"

namespace nativepg::detail {

struct multiplexer_state
{
    struct pending_read : boost::intrusive::list_base_hook<>
    {
        // Number of ReadyForQuery messages that we expect from
        // previously cancelled items
        std::size_t previous_rfqs{};

        // Setting it notifies the task to read next
        boost::capy::async_event evt{};
    };

    class read_queue_t
    {
        bool reading_{};
        boost::intrusive::list<pending_read> pending_;
        std::size_t trailing_rfqs_{};

    public:
        read_queue_t() = default;

        bool enter(pending_read& handle)
        {
            // Instruct the reader to read these many ReadyForQuery's
            // to keep the connection going
            handle.previous_rfqs = std::exchange(trailing_rfqs_, 0u);

            if (reading_)
            {
                // Someone's reading already. Add ourselves to the
                // end of the pending list and tell the reader to wait
                pending_.push_back(handle);
                return false;
            }
            else
            {
                // Ready to read
                reading_ = true;
                return true;
            }
        }

        void exit(pending_read& handle, std::size_t remaining_rfqs)
        {
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
    };

    boost::capy::async_mutex write_mtx;
    std::vector<unsigned char> pending_writes;
    read_queue_t read_queue_;

    // This is the writer side of exec, and should be called with the mutex acquired
    boost::capy::io_task<> write_request(boost::capy::any_stream& stream, const request& req)
    {
        BOOST_ASSERT(write_mtx.is_locked());

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

                // TODO: how do we report this?
                co_return {};
            }

            // We wrote what was remaining of the previous message
            pending_writes.clear();
        }

        // Now, our own message
        auto [ec, bytes] = co_await boost::capy::write(stream, boost::capy::make_buffer(req.payload()));
        if (ec)
        {
            // There was an error or cancellation. If we got an incomplete write,
            // we need to store what we didn't write so the connection doesn't break
            if (bytes < req.payload().size())
            {
                pending_writes.assign(req.payload().begin() + bytes, req.payload().end());
            }

            // TODO: how do we report this?
            co_return {};
        }

        // Done and succeeded
        co_return {};
    }

    // This is the reader side
    boost::capy::io_task<> read_response(
        boost::capy::any_stream& stream,
        pending_read* entry,
        const request& req,
        response_handler_ref handler
    )
    {
        // If someone else is writing, we need to wait for our turn
        if (is_reading)
        {
        }
    }

    boost::capy::io_task<> exec(
        boost::capy::any_stream& stream,
        const request& req,
        response_handler_ref handler
    )
    {
        // If someone else is writing, we need to wait
        auto [ec, guard] = co_await write_mtx.scoped_lock();
        if (ec)
            co_return {ec};

        // We're in the critical section. Decide whether we're going to read or wait
        boost::capy::async_event read_evt;
        pending_read entry{.evt = &read_evt};
        bool should_read = !is_reading;
        if (is_reading)
        {
            pending_reads.push_back(entry);
        }
        else
        {
            is_reading = true;
        }

        //
        auto res = co_await boost::capy::when_any()
    }
};

}  // namespace nativepg::detail

#endif  // BOOST_REDIS_MULTIPLEXER_HPP
