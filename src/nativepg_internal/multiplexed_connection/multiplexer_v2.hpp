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

    static std::size_t count_rfqs(const request& req) { return count_rfqs(req.messages()); }
    static std::size_t count_rfqs(std::span<const request_message_type>);

    enum class writer_result
    {
        nothing_written,
        partial_write,
        full_write,
    };

    // This is the writer side of exec, and should be called with the mutex acquired
    boost::capy::io_task<writer_result> write_request(
        boost::capy::any_stream& stream,
        boost::capy::async_mutex::lock_guard guard,  // forcibly release the mutex when the fn exits
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

                // To all effects, nothing was written - we expect no response for this request
                co_return {ec, writer_result::nothing_written};
            }

            // We wrote what was remaining of the previous message
            pending_writes.clear();
        }

        // Now, our own message
        auto [ec, bytes] = co_await boost::capy::write(stream, boost::capy::make_buffer(req.payload()));

        // There was a short write, probably due to an error or cancellation.
        // We need to store what we didn't write so the connection doesn't break
        if (bytes < req.payload().size())
        {
            pending_writes.assign(req.payload().begin() + bytes, req.payload().end());
            co_return {ec, writer_result::partial_write};
        }

        // Everything written (notice that we could, in principle, get an error here)
        co_return {ec, writer_result::full_write};
    }

    struct reader_result
    {
        std::size_t remaining_previous_rfqs;
        std::size_t remaining_rfqs;
    };

    // This is the reader side.
    // Returns the number of ReadyForQuery messages
    // that were left unread, so proper cleanup may happen
    boost::capy::io_task<reader_result> read_response(
        boost::capy::any_stream& stream,
        protocol::connection_state& st,
        pending_read& handle,
        bool is_first,
        const request& req,
        response_handler_ref handler
    )
    {
        return do_read_response(
            stream,
            st,
            is_first ? &handle.evt : nullptr,
            handle.previous_rfqs,
            req,
            handler
        );
    }

    boost::capy::io_task<> read_some_messages(
        boost::capy::any_stream& stream,
        protocol::connection_state& st
    );

    boost::capy::io_task<reader_result> do_read_response(
        boost::capy::any_stream& stream,
        protocol::connection_state& st,
        boost::capy::async_event* wait_evt,
        std::size_t previous_rfqs,
        const request& req,
        response_handler_ref handler
    )
    {
        // Wait if required
        if (wait_evt)
        {
            // We need to wait
            if (auto [ec] = co_await wait_evt->wait(); ec)
            {
                // We were cancelled before having any chance to run
                co_return {
                    ec,
                    reader_result{.remaining_previous_rfqs = previous_rfqs, .remaining_rfqs = count_rfqs(req)}
                };
            }
        }

        // Read any remaining ReadyForQuery messages
        std::size_t consumed = 0u;
        std::size_t remaining_prev_rfqs = previous_rfqs;
        while (remaining_prev_rfqs > 0u)
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
                    {
                        co_return {
                            ec,
                            reader_result{
                                          .remaining_previous_rfqs = remaining_prev_rfqs,
                                          .remaining_rfqs = count_rfqs(req)
                            }
                        };
                    }
                    continue;
                }
                else
                {
                    co_return {
                        res.ec,
                        reader_result{
                                      .remaining_previous_rfqs = remaining_prev_rfqs,
                                      .remaining_rfqs = count_rfqs(req)
                        }
                    };
                }
            }

            // We have a message
            st.update_tracked(res.message);
            if (res.message.type() == protocol::any_backend_message::kind::ready_for_query)
                --remaining_prev_rfqs;
        }

        // Now get to the messages concerning us
        protocol::read_response_fsm fsm{&req, handler, false};  // disallow COPY
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
                    {
                        co_return {
                            ec,
                            reader_result{
                                          .remaining_previous_rfqs = 0u,
                                          .remaining_rfqs = count_rfqs(fsm.get_remaining_messages())
                            }
                        };
                    }
                    continue;
                }
                else
                {
                    co_return {
                        res.ec,
                        reader_result{
                                      .remaining_previous_rfqs = 0u,
                                      .remaining_rfqs = count_rfqs(fsm.get_remaining_messages())
                        }
                    };
                }
            }

            // We have a message
            st.update_tracked(res.message);
            auto fsm_ec = fsm.resume(res.message);
            if (!fsm_ec)
            {
                // We've finished successfully
                st.read_buffer.consume(consumed);
                co_return {
                    fsm.get_handler_error().code, // TODO: diagnostics?
                    reader_result{.remaining_previous_rfqs = 0u, .remaining_rfqs = 0u}
                };
            }
            else if (fsm_ec != client_errc::needs_more)
            {
                // There has been a severe protocol violation (unrecoverable)
                st.read_buffer.consume(consumed);
                co_return {
                    fsm_ec,
                    reader_result{
                                  .remaining_previous_rfqs = 0u,
                                  .remaining_rfqs = count_rfqs(fsm.get_remaining_messages())
                    }
                };
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
        // If someone else is writing, we need to wait
        auto [ec, guard] = co_await write_mtx.scoped_lock();
        if (ec)
            co_return {ec};

        // Add ourselves to the read queue, if required.
        // This must happen under the mutex to prevent race conditions.
        // This must happen before writing, because responses
        //   might start arriving before the full write op finishes.
        pending_read handle;
        bool is_first = read_queue_.enter(handle);

        // Run the reader and writer tasks in parallel
        auto [final_ec, writer_res, reader_res] = co_await boost::capy::when_all(
            write_request(stream, std::move(guard), req),
            read_response(stream, st, handle, is_first, req, handler)
        );

        // Do the cleanup
        const std::size_t remaining_rfqs = writer_res == writer_result::nothing_written
                                               ? reader_res.remaining_previous_rfqs
                                               : reader_res.remaining_previous_rfqs +
                                                     reader_res.remaining_rfqs;
        read_queue_.exit(handle, remaining_rfqs);

        // TODO: retval
        co_return {};
    }
};

}  // namespace nativepg::detail

#endif  // BOOST_REDIS_MULTIPLEXER_HPP
