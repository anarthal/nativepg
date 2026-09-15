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

#include <cstddef>
#include <deque>
#include <vector>

#include "nativepg/protocol/views.hpp"
#include "nativepg/request.hpp"
#include "nativepg/responses/response_handler.hpp"

namespace nativepg::detail {

struct multiplexer_state
{
    struct pending_read
    {
        // This is a linked list
        pending_read* next{};
        pending_read* prev{};

        // If the item is a live request waiting for server output,
        // this is non-null and should be set once it's our turn
        boost::capy::async_event* evt{};

        // If the item is not live, the number of ready_for_query
        // messages that we should skip before jumping to the next item
        std::size_t remaining_rfqs{};
    };

    void list_push_back(pending_read& elm);  // TODO

    boost::capy::async_mutex write_mtx;
    std::vector<unsigned char> pending_writes;
    bool is_reading{};
    std::deque<pending_read> dead_pending_reads;  // storage
    pending_read* head{};
    pending_read* tail{};

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
            list_push_back(entry);
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
