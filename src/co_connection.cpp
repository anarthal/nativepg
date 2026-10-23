//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/assert.hpp>
#include <boost/capy/buffers.hpp>
#include <boost/capy/buffers/buffer_slice.hpp>
#include <boost/capy/buffers/make_buffer.hpp>
#include <boost/capy/cond.hpp>
#include <boost/capy/error.hpp>
#include <boost/capy/ex/execution_context.hpp>
#include <boost/capy/results.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/when_all.hpp>
#include <boost/capy/write.hpp>
#include <boost/container/static_vector.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <system_error>
#include <utility>

#include "nativepg/capy-stub/io/any_buffer_source.hpp"
#include "nativepg/client_errc.hpp"
#include "nativepg/co_connection.hpp"
#include "nativepg/connect_params.hpp"
#include "nativepg/detail/exec_state_impl.hpp"
#include "nativepg/encoding.hpp"
#include "nativepg/exclusivity.hpp"
#include "nativepg/exec_state.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/notification_vector.hpp"
#include "nativepg/protocol/connection_state.hpp"
#include "nativepg/protocol/copy.hpp"
#include "nativepg/protocol/detail/connect_fsm.hpp"
#include "nativepg/protocol/header.hpp"
#include "nativepg/protocol/parse_message.hpp"
#include "nativepg/protocol/sync.hpp"
#include "nativepg/protocol/terminate.hpp"
#include "nativepg/request.hpp"
#include "nativepg/responses/response_handler.hpp"
#include "nativepg/write_status.hpp"
#include "nativepg_internal/check_request.hpp"
#include "nativepg_internal/copy_in.hpp"
#include "nativepg_internal/copy_out.hpp"
#include "nativepg_internal/multiplexer.hpp"
#include "nativepg_internal/op_counter.hpp"
#include "nativepg_internal/pg_stream.hpp"

namespace capy = boost::capy;
namespace container = boost::container;

namespace nativepg {

static bool is_fatal(std::error_code ec) { return ec && ec != capy::cond::canceled; }

struct detail::co_connection_impl
{
    // Marks a state flag as true while the guard is alive.
    // Used by any op that needs 'already_running' detection
    struct running_guard
    {
        bool* running;
        explicit running_guard(bool& val) noexcept : running(&val) { *running = true; }
        running_guard(const running_guard&) = delete;
        running_guard& operator=(const running_guard&) = delete;
        ~running_guard() { *running = false; }
    };

    detail::pg_stream stream_;
    detail::op_counter pending_ops_;
    protocol::connection_state st{};
    detail::multiplexer mpx_;
    notification_vector exec_notifications_;
    bool receiver_running_{};
    bool is_connected_{};
    bool connect_shutdown_running_{};
    std::error_code last_error_;

    // TODO: this works until we implement TLS, then it'd need to be a coroutine
    void on_fatal_error(std::error_code ec)
    {
        // Guarantees first-error-wins semantics
        if (is_connected_)
        {
            last_error_ = ec;
            is_connected_ = false;

            // Session state is no longer reliable
            st.reset_tracked();

            mpx_.stop();
            stream_.close();
        }
    }

    explicit co_connection_impl(capy::execution_context& ctx) : stream_(ctx) {}

    capy::task<capy::results<>> connect(connect_params params, diagnostics* diag)
    {
        using protocol::detail::connect_fsm;

        // Can't be run in parallel with another connect/shutdown
        if (connect_shutdown_running_)
            co_return {client_errc::already_running};

        // We're now connecting
        running_guard guard{connect_shutdown_running_};

        // We might be tearing down a healthy session. Mark ourselves as
        // disconnected so no new operation is accepted while we tear down.
        is_connected_ = false;

        // Unblock anything currently outstanding.
        // These operations will complete immediately with an error after we yield.
        mpx_.stop();
        stream_.close();

        // Wait for pending operations to finish.
        // Avoids the risk of calling stream reads/writes concurrently,
        // and avoids use-after-free errors with write_some
        // accessing bytes that we free during cleanup.
        if (auto [ec] = co_await pending_ops_.wait(); ec)
        {
            last_error_ = ec;
            co_return {ec};
        }

        // We can forget about pending operations now - they don't need anything
        // from us anymore. Clean up the remaining data
        last_error_.clear();
        exec_notifications_.clear();
        st.reset();
        st.shared_diag = {};
        mpx_.restart();

        // Initialize
        connect_fsm fsm_(params);
        auto res = fsm_.resume(st, {}, 0u);

        while (true)
        {
            switch (res.type())
            {
                case connect_fsm::result_type::write:
                {
                    auto [ec, bytes] = co_await capy::write(stream_, capy::make_buffer(res.write_data()));
                    res = fsm_.resume(st, ec, bytes);
                    break;
                }
                case connect_fsm::result_type::read:
                {
                    auto [ec, bytes] = co_await stream_.read_some(capy::make_buffer(res.read_buffer()));
                    res = fsm_.resume(st, ec, bytes);
                    break;
                }
                case connect_fsm::result_type::connect:
                {
                    auto [ec] = co_await stream_.connect(params);
                    res = fsm_.resume(st, ec, 0u);
                    break;
                }
                case connect_fsm::result_type::close:
                {
                    stream_.close();
                    res = fsm_.resume(st, {}, 0u);
                    break;
                }
                case connect_fsm::result_type::done:
                {
                    // Update the connection status
                    last_error_ = res.error();
                    is_connected_ = !res.error();

                    // Done
                    if (diag)
                        *diag = st.shared_diag;
                    co_return {res.error()};
                }
                default: BOOST_ASSERT(false); co_return {};
            }
        }
    }

    capy::task<capy::results<>> shutdown()
    {
        // Shutdown can't run in parallel with connect() or other shutdown()
        if (connect_shutdown_running_)
            co_return {client_errc::already_running};
        running_guard guard{connect_shutdown_running_};

        std::error_code final_ec;

        // Can we send the Terminate message? If the write-side of the multiplexer
        // is locked, we might have a write in-progress or be in the middle of a frame.
        // In these cases, skip the graceful shutdown
        // Terminate only makes sense if the connection is healthy
        bool should_send_terminate = is_connected_ && !mpx_.is_write_locked();

        // Prevent any more requests from being accepted.
        // This avoid someone else acquiring the write mutex while we write the Terminate.
        is_connected_ = false;
        mpx_.stop();

        // Send the terminate
        if (should_send_terminate)
        {
            st.write_buffer.clear();
            [[maybe_unused]] auto ec = protocol::serialize(protocol::terminate{}, st.write_buffer);
            // TODO: not ideal, this shouldn't be allowed to fail in 1st place.
            // This is fixed size, could be constexpr
            BOOST_ASSERT(!ec);

            // TODO: what do we do with write_ec?
            auto [write_ec, bytes] = co_await capy::write(stream_, capy::make_buffer(st.write_buffer));
            final_ec = write_ec;
        }

        // Close the underlying transport anyway
        stream_.close();

        // Wait for any pending operations to finish.
        // As with connect, this is required before clearing buffers.
        if (auto [ec] = co_await pending_ops_.wait(); ec)
        {
            if (!final_ec)
                final_ec = ec;
            st.reset_tracked();
        }
        else
        {
            st.reset();
            mpx_.restart();
        }

        // These are safe anyway
        last_error_.clear();
        exec_notifications_.clear();

        co_return {final_ec};
    }

    std::error_code prepare_request(
        exec_state& exec_st,
        const request& req,
        response_handler_ref handler,
        exclusivity excl
    )
    {
        // Perform request setup
        if (auto ec = protocol::detail::setup_request(req, handler))
            return ec;

        // Set the state up. This cleans up any leftover from previous operations
        detail::exec_state_access::get_impl(exec_st).setup(*this, req, handler, excl);
        return std::error_code();
    }

    // This is the writer side of exec
    capy::task<capy::results<>> write_request(detail::exec_state_impl& exec_st)
    {
        using writer_status = detail::exec_state_impl::writer_status;

        // We must have been prepared, and must still have something to write
        if (!exec_st.is_prepared() || exec_st.payload_written())
            co_return {client_errc::invalid_state};

        // Abandoned translates into canceled
        if (exec_st.abandoned)
            co_return {capy::error::canceled};

        // The connection must be healthy
        if (!is_connected_)
            co_return {client_errc::not_connected};

        // Only one write operation per state may be in flight
        if (exec_st.writing)
            co_return {client_errc::already_running};
        running_guard guard{exec_st.writing};

        // In-flight operation accounting
        auto in_flight_guard = pending_ops_.scoped_notify_start();

        // Wait for our turn to write and register what we are doing in the queue.
        // When resuming a partially written request we still hold both, so skip this
        if (exec_st.writer_st == writer_status::initial)
        {
            if (auto [ec] = co_await mpx_.enter(exec_st); ec)
                co_return {ec};
        }
        BOOST_ASSERT(exec_st.writer_st == writer_status::locked);

        // Write any potential leftover from previous requests, plus whatever is left
        // of our own. The former is required to keep the connection healthy.
        // Most of the time, the 1st buffer is empty, and Corosio coalesces this to a
        // non-vectored write, for all backends.
        auto [ec, bytes_written] = co_await capy::write(
            stream_,
            std::array<capy::const_buffer, 2u>{
                capy::make_buffer(mpx_.previous_write_bytes()),
                capy::make_buffer(exec_st.remaining_payload())
            }
        );

        // Record what made it to the server, so a retry resumes where we left off
        mpx_.on_bytes_written(exec_st, bytes_written);

        // On failure we keep holding the write mutex, so no other request can
        // interleave its bytes with our half-written one. The caller may retry,
        // and destroying the exec_state hands the leftovers to the next writer
        if (ec)
        {
            if (is_fatal(ec))
                on_fatal_error(ec);
            co_return {ec};
        }

        // We wrote the request in full. Release the write side
        mpx_.report_writer_exit(exec_st);

        // Done
        co_return {};
    }

    capy::task<capy::results<>> read_some_response(detail::exec_state_impl& exec_st)
    {
        // We must have been prepared, and must still have something to read
        // This function is allowed in CopyIn mode
        if (!exec_st.is_prepared() || exec_st.read_done())
            co_return {client_errc::invalid_state};

        // Abandoned translates into canceled
        if (exec_st.abandoned)
            co_return {capy::error::canceled};

        // The connection must be healthy
        if (!is_connected_)
            co_return {client_errc::not_connected};

        // Only one reader per state may be in flight
        if (exec_st.reading)
            co_return {client_errc::already_running};
        running_guard guard{exec_st.reading};

        // In-flight operation accounting
        auto in_flight_guard = pending_ops_.scoped_notify_start();

        auto& fsm = *exec_st.fsm;

        // Wait for our turn (this is a no-op if it's out turn already)
        if (auto [ec] = co_await exec_st.evt.wait(); ec)
            co_return {ec};

        // Abandoned translates into canceled
        if (exec_st.abandoned)
            co_return {capy::error::canceled};

        // Read one batch of messages (this is a no-op if we have cached messages)
        if (auto [ec] = co_await read_some_messages(); ec)
        {
            // Report the error to the user, up to them to decide what to do.
            // Whatever we read so far is recorded in exec_st
            co_return {ec};
        }

        // Setup
        std::size_t consumed = 0u;

        while (true)
        {
            // Try to parse a cached message
            auto bytes = st.read_buffer.committed_area();
            auto res = protocol::parse_message(bytes.subspan(consumed));

            // Check for errors and end of input
            if (res.ec)
            {
                st.read_buffer.consume(consumed);
                consumed = 0u;
                if (res.ec == client_errc::needs_more)
                {
                    co_return {};  // Yield until the next call
                }
                else
                {
                    // This is a fatal error
                    on_fatal_error(res.ec);
                    co_return {res.ec};
                }
            }

            // We have a message
            consumed += res.size;
            st.update_tracked(res.message);
            bool is_rfq = res.message.type() == protocol::any_backend_message::kind::ready_for_query;

            // Store notifications so the receive loop can return them
            if (res.message.type() == protocol::any_backend_message::kind::notification_response)
            {
                exec_notifications_.push_back(res.message.get_notification_response());
                mpx_.notify_receiver();
                continue;
            }

            // Act on the message
            if (exec_st.pending_rfqs > 0u)
            {
                // A leftover message from previous execs
                if (is_rfq)
                    --exec_st.pending_rfqs;
            }
            else
            {
                // One of our messages
                if (is_rfq)
                    ++exec_st.read_rfqs;

                auto [fsm_ec, nsyncs] = fsm.resume(res.message);
                if (!fsm_ec)
                {
                    // We've finished successfully
                    st.read_buffer.consume(consumed);
                    exec_st.read_rfqs = static_cast<std::size_t>(-1);  // we've read everything
                    mpx_.report_reader_exit(exec_st);
                    co_return {};
                }
                else if (fsm_ec == client_errc::copy_in)
                {
                    // We've received a CopyInResponse
                    // Running these requires exclusive mode. Otherwise,
                    // other requests might end up being intercalated between copy data
                    if (exec_st.excl != exclusivity::exclusive)
                    {
                        // This is fatal
                        constexpr auto err = client_errc::requires_exclusive;
                        st.read_buffer.consume(consumed);
                        on_fatal_error(err);
                        co_return {err};
                    }

                    // Record the fact that we're now in CopyIn mode
                    exec_st.copy_in = {.active = true, .swallowed_syncs = nsyncs};

                    // The message batch should finish, and we will yield.
                    // We don't need to do it explicitly, though - that's guaranteed by
                    // our read_some semantics
                }
                else if (fsm_ec != client_errc::needs_more)
                {
                    // There has been a severe protocol violation (unrecoverable)
                    st.read_buffer.consume(consumed);
                    on_fatal_error(fsm_ec);
                    co_return {fsm_ec};
                }
            }
        }
    }

    static constexpr std::size_t max_frame_size = (std::numeric_limits<std::int32_t>::max)() - 4u;

    static std::array<unsigned char, 5u> make_copy_data_header(std::size_t size)
    {
        // TODO: I don't like this serialize_header
        BOOST_ASSERT(size <= max_frame_size);
        protocol::message_header h{protocol::copy_data_message_type, static_cast<std::int32_t>(size)};
        std::array<unsigned char, 5u> res;
        [[maybe_unused]] auto ec = protocol::serialize_header(h, res);
        BOOST_ASSERT(!ec);
        return res;
    }

    capy::task<capy::results<std::size_t>> write_some_copy_data(
        detail::exec_state_impl& exec_st,
        std::span<const capy::const_buffer> input_buffs
    )
    {
        // This has write_some semantics.
        // We clamp the input buffer sequence to this size.
        constexpr std::size_t max_input_buffers = 13u;

        // Buffer overhead is, at most, 3: 2 frame headers, and one
        // intermediate buffer split into two
        constexpr std::size_t max_output_buffers = max_input_buffers + 3;

        // The server must have asked us for data. This also implies that our request
        // was written in full, so our frames can't interleave with its payload
        if (exec_st.phase() != write_status::copy_data)
            co_return {client_errc::invalid_state, 0u};

        // If the connection becomes unhealthy after writing the request, we go to abandoned
        if (exec_st.abandoned)
            co_return {capy::error::canceled, 0u};

        // Only one write operation per state may be in flight
        if (exec_st.writing)
            co_return {client_errc::already_running, 0u};
        running_guard guard{exec_st.writing};

        // In-flight operation accounting
        auto in_flight_guard = pending_ops_.scoped_notify_start();

        // Clamp the input sequence
        if (input_buffs.size() > max_input_buffers)
            input_buffs = input_buffs.first(max_input_buffers);

        // Empty buffers are a no-op.
        // Special case required because we might be in the middle of
        // transferring a frame header
        const std::size_t buff_size = capy::buffer_size(input_buffs);
        if (buff_size == 0u)
            co_return {};

        std::array<unsigned char, 5u> prev_frame_header, frame_header;
        container::static_vector<capy::const_buffer, max_output_buffers> output_buffs;

        // Compute sizes
        const std::size_t prev_header_size = exec_st.copy_in.frame_num_bytes == 0u ||
                                                     exec_st.copy_in.transferred_bytes >= 5u
                                                 ? 0u
                                                 : 5u - exec_st.copy_in.transferred_bytes;
        const std::size_t prev_payload_size = exec_st.copy_in.frame_num_bytes == 0u ? 0u
                                              : exec_st.copy_in.transferred_bytes < 5u
                                                  ? exec_st.copy_in.frame_num_bytes
                                                  : exec_st.copy_in.frame_num_bytes + 5u -
                                                        exec_st.copy_in.transferred_bytes;
        const std::size_t prev_size = prev_header_size + prev_payload_size;
        const std::size_t payload_size = prev_payload_size >= buff_size
                                             ? 0u
                                             : (std::min)(buff_size - prev_payload_size, max_frame_size);
        const std::size_t total_size = prev_size + (payload_size == 0u ? 0u : payload_size + 5u);

        // Serialize what we need
        if (prev_header_size > 0u)
        {
            prev_frame_header = make_copy_data_header(exec_st.copy_in.frame_num_bytes);
            output_buffs.push_back(
                capy::make_buffer(
                    std::span<const unsigned char>(prev_frame_header)
                        .subspan(exec_st.copy_in.transferred_bytes)
                )
            );
        }

        if (prev_payload_size > 0u)
        {
            auto prev_payload = capy::buffer_slice(input_buffs, 0u, prev_payload_size);
            output_buffs.insert(output_buffs.end(), capy::begin(prev_payload), capy::end(prev_payload));
        }

        if (payload_size > 0u)
        {
            frame_header = make_copy_data_header(payload_size);
            output_buffs.push_back(capy::make_buffer(frame_header));
            auto payload = capy::buffer_slice(input_buffs, prev_payload_size, payload_size);
            output_buffs.insert(output_buffs.end(), capy::begin(payload), capy::end(payload));
        }

        // Write the thing
        auto [ec, bytes] = co_await stream_.write_some(std::span(output_buffs));
        std::size_t retval = 0u;

        if (bytes <= prev_header_size)
        {
            exec_st.copy_in.transferred_bytes += bytes;
            retval = 0u;
        }
        else if (bytes < prev_size)
        {
            exec_st.copy_in.transferred_bytes += bytes;
            retval = bytes - prev_header_size;
        }
        else if (bytes == prev_size)
        {
            exec_st.copy_in.frame_num_bytes = 0u;
            exec_st.copy_in.transferred_bytes = 0u;
            retval = prev_payload_size;
        }
        else if (bytes <= prev_size + 5u)
        {
            exec_st.copy_in.frame_num_bytes = payload_size;
            exec_st.copy_in.transferred_bytes = bytes - prev_size;
            retval = prev_payload_size;
        }
        else if (bytes < total_size)
        {
            exec_st.copy_in.frame_num_bytes = payload_size;
            exec_st.copy_in.transferred_bytes = bytes - prev_size;
            retval = bytes - prev_header_size - 5u;
        }
        else
        {
            BOOST_ASSERT(bytes == total_size);
            exec_st.copy_in.frame_num_bytes = 0u;
            exec_st.copy_in.transferred_bytes = 0u;
            retval = prev_payload_size + payload_size;
        }

        if (is_fatal(ec))
            on_fatal_error(ec);

        co_return {ec, retval};
    }

    capy::task<capy::results<>> write_copy_finished(
        detail::exec_state_impl& exec_st,
        bool success,
        std::string_view error_message
    )
    {
        // The server must have asked us for data. This also implies that our request
        // was written in full, so our terminator can't interleave with its payload
        if (exec_st.phase() != write_status::copy_data)
            co_return {client_errc::invalid_state};

        // If the connection becomes unhealthy after writing the request, we go to abandoned
        if (exec_st.abandoned)
            co_return {capy::error::canceled};

        // Only one write operation per state may be in flight
        if (exec_st.writing)
            co_return {client_errc::already_running};
        running_guard guard{exec_st.writing};

        // If there are missing bytes to transfer, that's an error
        if (exec_st.copy_in.frame_num_bytes > 0u)
            co_return {client_errc::copy_incomplete_transfer};

        // In-flight operation accounting
        auto in_flight_guard = pending_ops_.scoped_notify_start();

        // Compose the message to send. This is:
        //  CopyDone if we were successful
        //  CopyError if we failed
        //  In both cases, any number of Sync messages swallowed by the server
        st.write_buffer.clear();
        auto serialize_ec = success
                                ? protocol::serialize(protocol::copy_done{}, st.write_buffer)
                                : protocol::serialize(protocol::copy_fail{error_message}, st.write_buffer);
        if (serialize_ec)
            co_return {serialize_ec};
        for (std::size_t i = 0u; i < exec_st.copy_in.swallowed_syncs; ++i)
        {
            serialize_ec = protocol::serialize(protocol::sync{}, st.write_buffer);
            BOOST_ASSERT(!serialize_ec);  // TODO: not ideal
        }

        // Send this to the server
        auto [ec, bytes] = co_await capy::write(stream_, capy::make_buffer(st.write_buffer));
        if (ec)
        {
            // An incomplete transfer here is fatal
            // TODO: do we want resumability here, too?
            on_fatal_error(ec);
            co_return {ec};
        }

        // We're no longer in Copy-in mode
        exec_st.copy_in = {};

        co_return {};
    }

    capy::task<capy::results<>> receive(notification_vector& output)
    {
        // State check
        if (!is_connected_)
            co_return {client_errc::not_connected};

        // Verify that no two receivers run in parallel
        if (receiver_running_)
            co_return {client_errc::already_running};
        running_guard receiver_running_guard{receiver_running_};

        // In-flight operation accounting
        auto in_flight_guard = pending_ops_.scoped_notify_start();

        // We own the output from this point on
        output.clear();

        // Wait for notifications to arrive/be read
        auto [ec] = co_await wait_for_notifications(output);

        // If we managed to read any, report success.
        if (!output.empty())
            ec.clear();

        co_return {ec};
    }

    capy::task<capy::results<>> wait_for_notifications(notification_vector& output)
    {
        while (true)
        {
            // Wait for either notifications to arrive, or for our turn to read
            auto [ec, guard] = co_await mpx_.enter_receive();
            if (ec)
                co_return {ec};

            // Look for cached notifications first. Swapping hands the caller the
            // buffer that exec() filled, and gives exec() our (empty) one back
            if (!exec_notifications_.empty())
            {
                std::swap(exec_notifications_, output);
                exec_notifications_.clear();
                co_return {};
            }

            // Is it our turn to read? At this point, it probably is,
            // but race conditions with other exec()s could make it not the case
            if (guard.is_reading())
            {
                auto [loop_ec] = co_await receive_read(guard, output);
                if (loop_ec || !output.empty())
                    co_return {loop_ec};
            }

            // We yielded because we received a message that wasn't for us,
            // but we don't have anything to report
        }
    }

    capy::task<capy::results<>> receive_read(
        detail::multiplexer::receive_guard& guard,
        notification_vector& output
    )
    {
        std::size_t consumed = 0u;

        while (true)
        {
            // Try to parse a cached message
            auto bytes = st.read_buffer.committed_area();
            auto res = protocol::parse_message(bytes.subspan(consumed));

            // Check for errors and end of input
            if (res.ec)
            {
                st.read_buffer.consume(consumed);
                consumed = 0u;
                if (res.ec == client_errc::needs_more)
                {
                    // We need to read. If we have notifications here,
                    // return them to the user, and we'll read in the next iteration
                    if (!output.empty())
                        co_return {};

                    // No notifications. Do read
                    if (auto [ec] = co_await read_some_messages(); ec)
                        co_return {ec};
                    continue;
                }
                else
                {
                    // There has been a protocol violation
                    on_fatal_error(res.ec);
                    co_return {res.ec};
                }
            }

            // We've got a message. If it's one of the async messages,
            // handle it directly
            switch (res.message.type())
            {
                case protocol::any_backend_message::kind::notification_response:
                    output.push_back(res.message.get_notification_response());
                    consumed += res.size;
                    break;
                case protocol::any_backend_message::kind::parameter_status:
                    st.update_tracked(res.message);  // TODO: I don't like going through the variant here
                    consumed += res.size;
                    break;
                case protocol::any_backend_message::kind::notice_response:
                    consumed += res.size;
                    break;  // TODO: implement notices
                default:
                    if (guard.previous_rfqs() > 0u)
                    {
                        // We're reading leftovers
                        consumed += res.size;
                        if (res.message.type() == protocol::any_backend_message::kind::ready_for_query)
                        {
                            st.update_tracked(res.message);
                            guard.report_rfq();
                        }
                    }
                    else
                    {
                        // This is a request message that belongs to a reader. Bail out
                        // TODO: we're parsing the message twice here
                        st.read_buffer.consume(consumed);

                        // For safety, check that there is an actual reader.
                        // The multiplexer structure makes sure this should be the case.
                        // This prevents busy spinning in case of de-synchronization
                        if (mpx_.has_exec_readers())
                        {
                            co_return {};
                        }
                        else
                        {
                            constexpr auto err = client_errc::unexpected_message;
                            on_fatal_error(err);
                            co_return {err};
                        }
                    }
            }
        }
    }

    capy::task<capy::results<>> read_some_messages()
    {
        while (true)
        {
            // How many bytes are we missing to have a complete message?
            auto missing_bytes = protocol::message_missing_bytes(st.read_buffer.committed_area());
            if (missing_bytes == 0u)
                co_return {};

            // Make space in the buffer
            st.read_buffer.prepare(missing_bytes);

            // Read some data
            auto [ec, bytes] = co_await stream_.read_some(capy::make_buffer(st.read_buffer.prepared_area()));

            // Check for errors
            // TODO: we are discarding the read bytes unconditionally on error.
            // The bytes might contain a more useful explanation on what failed (i.e. an ErrorMessage)
            if (ec)
            {
                if (is_fatal(ec))
                    on_fatal_error(ec);
                co_return {ec};
            }

            // Commit the data we were handed in
            st.read_buffer.commit(bytes);
        }
    }
};

co_connection::co_connection(capy::execution_context& ctx)
    : impl_(std::make_unique<detail::co_connection_impl>(ctx))
{
}

co_connection::co_connection(co_connection&&) noexcept = default;

co_connection& co_connection::operator=(co_connection&&) noexcept = default;

co_connection::~co_connection() = default;

// TODO: I'd prefer having connect_params be a view
// const references here may cause dangling parameters
capy::task<capy::results<>> co_connection::connect(connect_params params, diagnostics* diag)
{
    return impl_->connect(std::move(params), diag);
}

capy::task<capy::results<>> co_connection::shutdown() { return impl_->shutdown(); }

capy::task<capy::results<>> co_connection::receive(notification_vector& output)
{
    return impl_->receive(output);
}

std::error_code co_connection::prepare_request(
    exec_state& exec_st,
    const request& req,
    response_handler_ref handler,
    exclusivity excl
)
{
    return impl_->prepare_request(exec_st, req, handler, excl);
}

capy::task<capy::results<>> co_connection::write_request(exec_state& st)
{
    return impl_->write_request(detail::exec_state_access::get_impl(st));
}

capy::task<capy::results<>> co_connection::read_some_response(exec_state& st)
{
    return impl_->read_some_response(detail::exec_state_access::get_impl(st));
}

static capy::task<capy::results<>> read_response(co_connection& conn, exec_state& exec_st, diagnostics* diag)
{
    while (!exec_st.read_done())
    {
        if (auto [ec] = co_await conn.read_some_response(exec_st); ec)
            co_return {ec};
    }

    const auto& handler_err = exec_st.handler_error();
    if (diag)
        *diag = handler_err.diag;
    co_return {handler_err.code};
}

capy::task<capy::results<>> co_connection::exec(
    const request& req,
    response_handler_ref handler,
    diagnostics* diag
)
{
    // Setup
    exec_state exec_st;
    if (auto ec = prepare_request(exec_st, req, handler, exclusivity::shared))
        co_return {ec};

    // Run the reader and writer tasks in parallel
    auto [final_ec, writer_dummy, reader_dummy] = co_await capy::when_all(
        write_request(exec_st),
        read_response(*this, exec_st, diag)
    );

    co_return {final_ec};
}

capy::task<capy::results<std::uint64_t>> co_connection::copy_in(
    const request& req,
    capy::any_buffer_source source,
    diagnostics* diag
)
{
    return detail::copy_in(*this, req, std::move(source), diag);
}

capy::task<capy::results<std::uint64_t>> co_connection::copy_out(
    const request& req,
    capy::any_write_sink sink,
    diagnostics* diag
)
{
    return detail::copy_out(*this, req, std::move(sink), diag);
}

capy::task<capy::results<std::size_t>> co_connection::write_some_copy_data_impl(
    exec_state& st,
    std::span<const capy::const_buffer> buffers
)
{
    return impl_->write_some_copy_data(detail::exec_state_access::get_impl(st), buffers);
}

capy::task<capy::results<>> co_connection::write_copy_done(exec_state& st)
{
    return impl_->write_copy_finished(detail::exec_state_access::get_impl(st), true, {});
}

capy::task<capy::results<>> co_connection::write_copy_fail(exec_state& st, std::string_view message)
{
    return impl_->write_copy_finished(detail::exec_state_access::get_impl(st), false, message);
}

protocol::connection_state& co_connection::state() { return impl_->st; }

std::optional<bool> co_connection::standard_conforming_strings() const
{
    return impl_->st.standard_conforming_strings;
}

std::optional<encoding> co_connection::client_encoding() const { return impl_->st.client_encoding; }

protocol::transaction_status co_connection::txn_status() const noexcept { return impl_->st.txn_status; }

bool co_connection::is_connected() const noexcept { return impl_->is_connected_; }

std::error_code co_connection::last_error() const noexcept { return impl_->last_error_; }

void detail::exec_state_impl::reset()
{
    BOOST_ASSERT(!writing);
    BOOST_ASSERT(!reading);

    // Release whatever we still hold in the multiplexer.
    // Being linked is what tells us that enter() succeeded
    if (is_prepared() && is_linked() && !abandoned)
    {
        if (copy_in.active)
        {
            // Abandoning during Copy-in is not recoverable.
            // This already unlinks and cleans everything up.
            conn->on_fatal_error(client_errc::copy_incomplete_transfer);
        }
        else
        {
            // The writer may still hold the write mutex
            if (writer_st == writer_status::locked)
                conn->mpx_.report_writer_exit(*this);

            // The reader may still owe an exit report
            if (!reader_done)
                conn->mpx_.report_reader_exit(*this);
        }
    }
    BOOST_ASSERT(!is_linked());

    conn = nullptr;
    excl = exclusivity::shared;
    writer_st = writer_status::initial;
    reader_done = false;
    copy_in = {};
    bytes_written = 0u;
    pending_rfqs = 0u;
    read_rfqs = 0u;
    abandoned = false;
    evt.clear();
    fsm.reset();
}

void detail::exec_state_impl::setup(
    co_connection_impl& conn_ref,
    const request& req,
    response_handler_ref handler,
    exclusivity new_excl
)
{
    // Clean up any leftover from previous operations
    reset();

    conn = &conn_ref;
    excl = new_excl;
    fsm.emplace(&req, handler);
}

}  // namespace nativepg
