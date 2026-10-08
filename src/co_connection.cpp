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
#include <boost/capy/ex/async_event.hpp>
#include <boost/capy/ex/execution_context.hpp>
#include <boost/capy/io/any_buffer_source.hpp>
#include <boost/capy/io_task.hpp>
#include <boost/capy/when_all.hpp>
#include <boost/capy/write.hpp>
#include <boost/container/static_vector.hpp>
#include <boost/corosio/connect.hpp>
#include <boost/corosio/resolver.hpp>
#include <boost/corosio/socket_option.hpp>
#include <boost/corosio/tcp_socket.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

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
#include "nativepg/responses/any_request_message.hpp"
#include "nativepg/responses/command_info.hpp"
#include "nativepg/responses/detail/response_utils.hpp"
#include "nativepg/responses/response_handler.hpp"
#include "nativepg/write_status.hpp"
#include "nativepg_internal/check_request.hpp"
#include "nativepg_internal/multiplexer.hpp"

namespace capy = boost::capy;
namespace corosio = boost::corosio;
namespace container = boost::container;

namespace nativepg {

// write_some_copy_data has write_some semantics, and clamps the input
// buffer sequence to this number of buffers
static constexpr std::size_t max_copy_input_buffers = 13u;

struct co_connection::impl
{
    // Marks a state as having a write operation in flight, so that no two of
    // write_request/write_some_copy_data/write_copy_done/write_copy_fail overlap
    struct writing_guard
    {
        detail::exec_state_impl* st;
        explicit writing_guard(detail::exec_state_impl& s) noexcept : st(&s) { st->writing = true; }
        writing_guard(const writing_guard&) = delete;
        writing_guard& operator=(const writing_guard&) = delete;
        ~writing_guard() { st->writing = false; }
    };

    corosio::resolver resolv;
    corosio::tcp_socket sock;
    protocol::connection_state st{};
    capy::any_stream stream{&sock};
    detail::multiplexer mpx_;  // TODO: clean up this?
    notification_vector exec_notifications_;
    bool receiver_running_{};

    void reset()
    {
        // TODO: this somehow conflicts with connection_state::reset()
        // For now we keep both, as this is specific to co_connection, but the ideal
        // is having just one
        exec_notifications_.clear();
    }

    explicit impl(capy::execution_context& ctx) : resolv(ctx), sock(ctx) {}

    capy::io_task<> physical_connect(const connect_params& params)
    {
        auto [ec, endpoints] = co_await resolv.resolve(params.hostname, std::to_string(params.port));
        if (ec)
            co_return {ec};

        auto [ec2, ep] = co_await boost::corosio::connect(sock, endpoints);
        if (ec2)
            co_return {ec2};

        // Disable Nagle's algorithm.
        // Must be done after async_connect because it re-opens the socket
        // for every candidate endpoint, discarding options.
        sock.set_option(corosio::socket_option::no_delay(true));

        co_return {};
    }

    capy::io_task<> shutdown()
    {
        // TODO: we should probably have some state checks
        // TODO: we could really serialize to a fixed storage block, this is known size
        // TODO: an error here shouldn't prevent the function from closing the transport
        //       (the error should not happen, to begin with)
        // TODO: this is bypassing the multiplexer and it should't
        // Serialize the terminate request
        st.write_buffer.clear();
        if (auto ec = protocol::serialize(protocol::terminate{}, st.write_buffer))
            co_return {ec};

        // Write it
        auto [write_ec, bytes] = co_await capy::write(stream, capy::make_buffer(st.write_buffer));

        // GUCs should be reported as unknown for unestablished connections
        st.reset_gucs();

        // Close the underlying transport anyway.
        // No tcp_socket::shutdown() here to match what libpq does.
        // At least on Linux, it does nothing:
        // both close() and shutdown(SHUT_RDWR) will send a RST if there is pending
        // data in the read buffer (e.g. a pending NotificationResponse),
        // and a FIN otherwise. Should't be a big deal.
        sock.close();

        co_return {write_ec};
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
        detail::exec_state_access::get_impl(exec_st).setup(mpx_, req, handler, excl);
        return std::error_code();
    }

    // This is the writer side of exec
    boost::capy::io_task<> write_request(detail::exec_state_impl& exec_st)
    {
        using writer_status = detail::exec_state_impl::writer_status;

        // We must have been prepared, and must still have something to write
        if (!exec_st.is_prepared() || exec_st.payload_written())
            co_return {client_errc::invalid_state};

        // Only one write operation per state may be in flight
        if (exec_st.writing)
            co_return {client_errc::already_running};
        writing_guard guard{exec_st};

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
        auto [ec, bytes_written] = co_await boost::capy::write(
            stream,
            std::array<boost::capy::const_buffer, 2u>{
                boost::capy::make_buffer(mpx_.previous_write_bytes()),
                boost::capy::make_buffer(exec_st.remaining_payload())
            }
        );

        // Record what made it to the server, so a retry resumes where we left off
        mpx_.on_bytes_written(exec_st, bytes_written);

        // On failure we keep holding the write mutex, so no other request can
        // interleave its bytes with our half-written one. The caller may retry,
        // and destroying the exec_state hands the leftovers to the next writer
        if (ec)
            co_return {ec};

        // We wrote the request in full. Release the write side
        mpx_.report_writer_exit(exec_st);

        // Done
        co_return {};
    }

    boost::capy::io_task<> read_some_response(detail::exec_state_impl& exec_st)
    {
        // We must have been prepared, and must still have something to read
        // This function is allowed in CopyIn mode
        if (!exec_st.is_prepared() || exec_st.read_done())
            co_return {client_errc::invalid_state};

        // Only one reader per state may be in flight
        if (exec_st.reading)
            co_return {client_errc::already_running};
        exec_st.reading = true;
        struct reading_guard
        {
            detail::exec_state_impl* st;
            ~reading_guard() { st->reading = false; }
        } guard{&exec_st};

        auto& fsm = *exec_st.fsm;

        // Wait for our turn (this is a no-op if it's out turn already)
        if (auto [ec] = co_await exec_st.evt.wait(); ec)
            co_return {ec};

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
                    // TODO: this is a fatal error and should be recorded as such
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
                        // TODO: this is a fatal error
                        st.read_buffer.consume(consumed);
                        co_return {client_errc::requires_exclusive};
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
                    // TODO: flag this internally
                    st.read_buffer.consume(consumed);
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

    capy::io_task<std::size_t> write_some_copy_data(
        detail::exec_state_impl& exec_st,
        std::span<const capy::const_buffer> input_buffs
    )
    {
        // Buffer overhead is, at most, 3: 2 frame headers, and one
        // intermediate buffer split into two
        constexpr std::size_t max_output_buffers = max_copy_input_buffers + 3;

        // The server must have asked us for data. This also implies that our request
        // was written in full, so our frames can't interleave with its payload
        if (exec_st.phase() != write_status::copy_data)
            co_return {client_errc::invalid_state, 0u};

        // Only one write operation per state may be in flight
        if (exec_st.writing)
            co_return {client_errc::already_running, 0u};
        writing_guard guard{exec_st};

        // Clamp the input sequence
        if (input_buffs.size() > max_copy_input_buffers)
            input_buffs = input_buffs.first(max_copy_input_buffers);

        // Empty buffers are a no-op.
        // Special case required because we might be in the middle of
        // transferring a frame header
        const std::size_t buff_size = boost::capy::buffer_size(input_buffs);
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
            auto data = prev_payload.data();
            output_buffs.insert(output_buffs.end(), capy::begin(data), capy::end(data));
        }

        if (payload_size > 0u)
        {
            frame_header = make_copy_data_header(payload_size);
            output_buffs.push_back(capy::make_buffer(frame_header));
            auto payload = capy::buffer_slice(input_buffs, prev_payload_size, payload_size);
            auto data = payload.data();
            output_buffs.insert(output_buffs.end(), capy::begin(data), capy::end(data));
        }

        // Write the thing
        auto [ec, bytes] = co_await stream.write_some(std::span(output_buffs));
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

        co_return {ec, retval};
    }

    boost::capy::io_task<> write_copy_finished(
        detail::exec_state_impl& exec_st,
        bool success,
        std::string_view error_message
    )
    {
        // The server must have asked us for data. This also implies that our request
        // was written in full, so our terminator can't interleave with its payload
        if (exec_st.phase() != write_status::copy_data)
            co_return {client_errc::invalid_state};

        // Only one write operation per state may be in flight
        if (exec_st.writing)
            co_return {client_errc::already_running};
        writing_guard guard{exec_st};

        // If there are missing bytes to transfer, that's an error
        if (exec_st.copy_in.frame_num_bytes > 0u)
            co_return {client_errc::copy_incomplete_transfer};

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
        auto [ec, bytes] = co_await capy::write(stream, capy::make_buffer(st.write_buffer));
        if (ec)
        {
            // TODO: an incomplete transfer here is fatal. Mark the connection
            // as dead when we have the functionality
            // TODO: do we want resumability here, too?
            co_return {ec};
        }

        // We're no longer in Copy-in mode
        exec_st.copy_in = {};

        co_return {};
    }

    boost::capy::io_task<> receive(notification_vector& output)
    {
        // Verify that no two receivers run in parallel
        if (receiver_running_)
            co_return {client_errc::already_running};
        receiver_running_ = true;

        // Release the slot however we leave this function
        struct receiver_guard
        {
            impl* self;
            ~receiver_guard() { self->receiver_running_ = false; }
        } receiver_running_guard{this};

        // We own the output from this point on
        output.clear();

        // Wait for notifications to arrive/be read
        auto [ec] = co_await wait_for_notifications(output);

        // If we managed to read any, report success.
        // TODO: if this was a fatal error, we should mark the connection as failed
        if (!output.empty())
            ec.clear();

        co_return {ec};
    }

    boost::capy::io_task<> wait_for_notifications(notification_vector& output)
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

    boost::capy::io_task<> receive_read(
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

            // Check for errors and end of input.
            // Errors here are irrecoverable.
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
                            guard.report_rfq();
                    }
                    else
                    {
                        // This is a request message that belongs to a reader. Bail out
                        // TODO: we're parsing the message twice here
                        st.read_buffer.consume(consumed);

                        // For safety, check that there is an actual reader.
                        // The multiplexer structure makes sure this should be the case.
                        // This prevents busy spinning in case of de-synchronization
                        auto final_ec = mpx_.has_exec_readers() ? std::error_code()
                                                                : client_errc::unexpected_message;
                        co_return {final_ec};
                    }
            }
        }
    }

    capy::io_task<> read_some_messages()
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
            auto [ec, bytes] = co_await stream.read_some(capy::make_buffer(st.read_buffer.prepared_area()));

            // Check for errors
            if (ec)
                co_return {ec};

            // Commit the data we were handed in
            st.read_buffer.commit(bytes);
        }
    }
};

co_connection::co_connection(capy::execution_context& ctx) : impl_(std::make_unique<impl>(ctx)) {}

co_connection::co_connection(co_connection&&) noexcept = default;

co_connection& co_connection::operator=(co_connection&&) noexcept = default;

co_connection::~co_connection() = default;

// TODO: I'd prefer having connect_params be a view
// const references here may cause dangling parameters
// TODO: proper reset
capy::io_task<> co_connection::connect(connect_params params, diagnostics* diag)
{
    using protocol::detail::connect_fsm;

    // Initialize
    impl_->reset();
    connect_fsm fsm_(params);
    auto res = fsm_.resume(impl_->st, {}, 0u);

    while (true)
    {
        switch (res.type())
        {
            case connect_fsm::result_type::write:
            {
                auto [ec, bytes] = co_await capy::write(impl_->sock, capy::make_buffer(res.write_data()));
                res = fsm_.resume(impl_->st, ec, bytes);
                break;
            }
            case connect_fsm::result_type::read:
            {
                auto [ec, bytes] = co_await impl_->sock.read_some(capy::make_buffer(res.read_buffer()));
                res = fsm_.resume(impl_->st, ec, bytes);
                break;
            }
            case connect_fsm::result_type::connect:
            {
                auto [ec] = co_await impl_->physical_connect(params);
                res = fsm_.resume(impl_->st, ec, 0u);
                break;
            }
            case connect_fsm::result_type::close:
            {
                impl_->sock.close();  // this can't fail in Corosio
                res = fsm_.resume(impl_->st, {}, 0u);
                break;
            }
            case connect_fsm::result_type::done:
            {
                if (diag)
                    *diag = impl_->st.shared_diag;
                co_return {res.error()};
            }
            default: BOOST_ASSERT(false); co_return {};
        }
    }
}

capy::io_task<> co_connection::shutdown() { return impl_->shutdown(); }

capy::io_task<> co_connection::receive(notification_vector& output) { return impl_->receive(output); }

std::error_code co_connection::prepare_request(
    exec_state& exec_st,
    const request& req,
    response_handler_ref handler,
    exclusivity excl
)
{
    return impl_->prepare_request(exec_st, req, handler, excl);
}

boost::capy::io_task<> co_connection::write_request(exec_state& st)
{
    return impl_->write_request(detail::exec_state_access::get_impl(st));
}

boost::capy::io_task<> co_connection::read_some_response(exec_state& st)
{
    return impl_->read_some_response(detail::exec_state_access::get_impl(st));
}

static capy::io_task<> read_response(co_connection& conn, exec_state& exec_st, diagnostics* diag)
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

capy::io_task<> co_connection::exec(const request& req, response_handler_ref handler, diagnostics* diag)
{
    // Setup
    exec_state exec_st;
    if (auto ec = prepare_request(exec_st, req, handler, exclusivity::shared))
        co_return {ec};

    // Run the reader and writer tasks in parallel
    auto [final_ec, writer_dummy, reader_dummy] = co_await boost::capy::when_all(
        write_request(exec_st),
        read_response(*this, exec_st, diag)
    );

    co_return {final_ec};
}

namespace {

// Response handler used by copy_in. Covers the entire request, so it works with
// any request shape, and tracks what copy_in needs to know: whether the server
// rejected the copy (while the response is still being read), how many times
// the server asked us for data, and the number of rows that were copied.
class copy_in_handler
{
    extended_error err_{};
    command_info info_{};
    std::uint64_t rows_{};
    std::size_t num_copies_{};
    bool in_copy_{};
    bool copy_failed_{};

public:
    copy_in_handler() = default;

    handler_setup_result setup(const request& req, std::size_t)
    {
        // TODO: if there isn't at least one Execute or Query, this should fail
        // TODO: *this = X seems sus
        *this = copy_in_handler();
        return req.messages().size();
    }

    void on_message(const any_request_message& msg, std::size_t, extended_error& err)
    {
        switch (msg.type())
        {
            case any_request_message::kind::copy_in_response:
                ++num_copies_;
                in_copy_ = true;
                break;
            case any_request_message::kind::command_complete:
                // The tag that closes a copy carries the number of copied rows
                if (in_copy_)
                {
                    // TODO: can be simplified
                    detail::from_command_complete(info_, msg.get_command_complete());
                    rows_ = info_.affected_rows.value_or(0u);
                    in_copy_ = false;
                }
                break;
            case any_request_message::kind::error_response:
                detail::store_error(msg.get_error_response(), err);

                // Keep our own copy: exec_state::handler_error() may only be
                // accessed once the entire response has been read, and the writer
                // needs to know about errors as soon as they arrive
                if (!err_.code)
                    err_ = err;

                // Errors raised by other statements of the request (possible when
                // pipelining) don't require us to stop sending data
                if (in_copy_)
                {
                    copy_failed_ = true;
                    in_copy_ = false;
                }
                break;
            default: break;
        }
    }

    // Did the server reject the copy? May be called while the response is being read
    bool copy_failed() const { return copy_failed_; }

    // How many times did the server ask us for data?
    std::size_t num_copies() const { return num_copies_; }

    // Rows reported by the CommandComplete that closed the copy
    std::uint64_t rows() const { return rows_; }

    // The first error that the server reported, if any
    const extended_error& error() const { return err_; }
};

// Feeds the server with data from source until the source is exhausted, the server
// rejects the copy, or the source fails. Terminates the copy in all cases.
// Only transport failures are reported: anything else is recorded in copy_ec.
capy::io_task<> run_copy(
    co_connection& conn,
    exec_state& st,
    capy::any_buffer_source& source,
    const copy_in_handler& handler,
    std::error_code& copy_ec
)
{
    std::array<capy::const_buffer, max_copy_input_buffers> pull_storage, pending_storage;

    while (true)
    {
        // If the server rejected our data, stop as soon as we notice it.
        // We can't stop any earlier: whatever we handed to write_some_copy_data
        // has already been framed, and must be transferred before terminating
        if (handler.copy_failed())
            co_return co_await conn.write_copy_fail(st, "aborted by the client after a server error");

        // Get more data
        auto [pull_ec, buffs] = co_await source.pull(pull_storage);
        if (pull_ec == capy::cond::eof)  // TODO: does this guarantee buffer_size(buffs) == 0?
            co_return co_await conn.write_copy_done(st);
        if (pull_ec)
        {
            copy_ec = pull_ec;
            co_return co_await conn.write_copy_fail(st, pull_ec.message());
        }

        // Clamping matches what write_some_copy_data accepts in a single call,
        // so we always know how many bytes we committed. We copy the descriptors
        // because we advance through them as they are transferred
        if (buffs.size() > max_copy_input_buffers)
            buffs = buffs.first(max_copy_input_buffers);
        std::size_t num_pending = 0u;
        for (auto buff : buffs)
        {
            if (buff.size() > 0u)
                pending_storage[num_pending++] = buff;
        }
        std::span<capy::const_buffer> pending(pending_storage.data(), num_pending);
        const std::size_t pulled = capy::buffer_size(pending);

        // A conforming source doesn't report success without data
        BOOST_ASSERT(pulled > 0u);

        // Bytes handed to write_some_copy_data can't be withdrawn, so transfer the
        // entire batch before pulling again. This way, the calls that terminate the
        // copy never find a half-written frame
        while (!pending.empty())
        {
            auto [write_ec, bytes] = co_await conn.write_some_copy_data(
                st,
                std::span<const capy::const_buffer>(pending)
            );

            // The frame may have been written partially, and we have no data
            // to complete it with. The connection is unusable
            if (write_ec)
                co_return {write_ec};

            // Discard what reached the server
            while (bytes > 0u)
            {
                auto& front = pending.front();
                if (front.size() > bytes)
                {
                    front = capy::const_buffer(
                        static_cast<const unsigned char*>(front.data()) + bytes,
                        front.size() - bytes
                    );
                    bytes = 0u;
                }
                else
                {
                    bytes -= front.size();
                    pending = pending.subspan(1u);
                }
            }
        }

        source.consume(pulled);
    }
}

// Writer side of copy_in
capy::io_task<> copy_in_writer(
    co_connection& conn,
    exec_state& st,
    capy::any_buffer_source& source,
    const copy_in_handler& handler,
    capy::async_event& reader_progressed,
    std::error_code& copy_ec
)
{
    // Send the request
    if (auto [ec] = co_await conn.write_request(st); ec)
        co_return {ec};

    std::size_t num_copies = 0u;

    while (true)
    {
        switch (st.write_phase())
        {
            case write_status::done: co_return {};
            case write_status::waiting_for_reader:
                // Only the reader can tell whether the server wants data from us.
                // The event is sticky, so clear it before waiting. There is no
                // suspension point between the check above and the clear,
                // so no notification can be missed
                reader_progressed.clear();
                if (auto [ec] = co_await reader_progressed.wait(); ec)
                    co_return {ec};
                continue;
            default: break;
        }

        // The server is asking us for data
        if (++num_copies > 1u)
        {
            // We only know how to feed a single copy. Terminate this one anyway,
            // so the connection is left in sync, and report the problem
            if (!copy_ec)
                copy_ec = client_errc::invalid_copy_in_request;
            if (auto [ec] = co_await conn.write_copy_fail(st, "unexpected COPY ... FROM STDIN"); ec)
                co_return {ec};
        }
        else if (auto [ec] = co_await run_copy(conn, st, source, handler, copy_ec); ec)
        {
            co_return {ec};
        }
    }
}

// Reader side of copy_in
capy::io_task<> copy_in_reader(co_connection& conn, exec_state& st, capy::async_event& reader_progressed)
{
    while (!st.read_done())
    {
        auto [ec] = co_await conn.read_some_response(st);

        // Let the writer re-evaluate what it should do next
        reader_progressed.set();

        if (ec)
            co_return {ec};
    }

    co_return {};
}

}  // namespace

capy::io_task<std::uint64_t> co_connection::copy_in(
    const request& req,
    capy::any_buffer_source source,
    diagnostics* diag
)
{
    // Setup. Copy operations can't be multiplexed with other requests
    copy_in_handler handler;
    exec_state st;
    if (auto ec = prepare_request(st, req, &handler, exclusivity::exclusive))
        co_return {ec, 0u};

    // Run the reader and the writer in parallel. The writer only reports transport
    // failures, so that a rejected copy doesn't cancel the reader: draining the
    // response is what keeps the connection usable
    capy::async_event reader_progressed;
    std::error_code copy_ec;
    auto [final_ec, writer_dummy, reader_dummy] = co_await capy::when_all(
        copy_in_writer(*this, st, source, handler, reader_progressed, copy_ec),
        copy_in_reader(*this, st, reader_progressed)
    );

    // The connection is left in an unspecified state if this happens
    if (final_ec)
        co_return {final_ec, 0u};

    if (diag)
        *diag = copy_ec ? diagnostics() : handler.error().diag;

    // The source failed, or the request is not one we can drive. The error that our
    // CopyFail triggered is an echo of this one, so don't report it instead
    if (copy_ec)
        co_return {copy_ec, 0u};

    // The server rejected the request or the data we sent
    if (handler.error().code)
        co_return {handler.error().code, 0u};

    // The request never asked us for data
    if (handler.num_copies() != 1u)
        co_return {client_errc::invalid_copy_in_request, 0u};

    co_return {std::error_code(), handler.rows()};
}

capy::io_task<std::size_t> co_connection::write_some_copy_data_impl(
    exec_state& st,
    std::span<const capy::const_buffer> buffers
)
{
    return impl_->write_some_copy_data(detail::exec_state_access::get_impl(st), buffers);
}

capy::io_task<> co_connection::write_copy_done(exec_state& st)
{
    return impl_->write_copy_finished(detail::exec_state_access::get_impl(st), true, {});
}

capy::io_task<> co_connection::write_copy_fail(exec_state& st, std::string_view message)
{
    return impl_->write_copy_finished(detail::exec_state_access::get_impl(st), false, message);
}

capy::io_task<> co_connection::read_some_messages() { return impl_->read_some_messages(); }

capy::any_stream& co_connection::stream() { return impl_->stream; }

protocol::connection_state& co_connection::state() { return impl_->st; }

std::optional<bool> co_connection::standard_conforming_strings() const
{
    return impl_->st.standard_conforming_strings;
}

std::optional<encoding> co_connection::client_encoding() const { return impl_->st.client_encoding; }

// TODO: do we want another cpp for this?
void detail::exec_state_impl::reset()
{
    BOOST_ASSERT(!writing);
    BOOST_ASSERT(!reading);

    // Release whatever we still hold in the multiplexer.
    // Being linked is what tells us that enter() succeeded
    if (is_prepared() && is_linked())
    {
        // The writer may still hold the write mutex
        if (writer_st == writer_status::locked)
            mpx->report_writer_exit(*this);

        // The reader may still owe an exit report
        if (!reader_done)
            mpx->report_reader_exit(*this);
    }
    BOOST_ASSERT(!is_linked());

    mpx = nullptr;
    excl = exclusivity::shared;
    writer_st = writer_status::initial;
    reader_done = false;
    copy_in = {};
    bytes_written = 0u;
    pending_rfqs = 0u;
    read_rfqs = 0u;
    evt.clear();
    fsm.reset();
}

void detail::exec_state_impl::setup(
    multiplexer& mpx_ref,
    const request& req,
    response_handler_ref handler,
    exclusivity new_excl
)
{
    // Clean up any leftover from previous operations
    reset();

    mpx = &mpx_ref;
    excl = new_excl;
    fsm.emplace(&req, handler);
}

}  // namespace nativepg
