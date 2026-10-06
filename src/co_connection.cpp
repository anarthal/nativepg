//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/assert.hpp>
#include <boost/capy/buffers.hpp>
#include <boost/capy/buffers/make_buffer.hpp>
#include <boost/capy/ex/execution_context.hpp>
#include <boost/capy/io_task.hpp>
#include <boost/capy/write.hpp>
#include <boost/corosio/connect.hpp>
#include <boost/corosio/resolver.hpp>
#include <boost/corosio/socket_option.hpp>
#include <boost/corosio/tcp_socket.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

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
#include "nativepg_internal/check_request.hpp"
#include "nativepg_internal/multiplexer.hpp"

namespace capy = boost::capy;
namespace corosio = boost::corosio;

namespace nativepg {

struct co_connection::impl
{
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

        // Only one writer per state may be in flight
        if (exec_st.writing)
            co_return {client_errc::already_running};
        exec_st.writing = true;
        struct writing_guard
        {
            detail::exec_state_impl* st;
            ~writing_guard() { st->writing = false; }
        } guard{&exec_st};

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

                    // Yield, it's now the user's turn to write data
                    // TODO: but the user may want to keep reading to look for errors
                    st.read_buffer.consume(consumed);
                    co_return {};
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

    boost::capy::io_task<std::size_t> write_some_copy_data(
        detail::exec_state_impl& exec_st,
        boost::capy::const_buffer buff
    )
    {
        // State check
        if (!exec_st.copy_in.active)
            co_return {client_errc::invalid_state, 0u};

        // Empty buffers are a no-op
        if (buff.size() == 0u)
            co_return {};

        std::vector<capy::const_buffer> bufs;
        std::array<unsigned char, 5u> frame_header, prev_frame_header;
        std::span<const unsigned char> buff_as_span{
            static_cast<const unsigned char*>(buff.data()),
            buff.size()
        };

        // Compute sizes
        const std::size_t prev_header_size = exec_st.copy_in.frame_num_bytes == 0u ||
                                                     exec_st.copy_in.transferred_bytes >= 5u
                                                 ? 0u
                                                 : 5u - exec_st.copy_in.transferred_bytes;
        const std::size_t prev_payload_size = exec_st.copy_in.frame_num_bytes == 0u ||
                                                      exec_st.copy_in.transferred_bytes < 5u
                                                  ? 0u
                                                  : exec_st.copy_in.frame_num_bytes + 5u -
                                                        exec_st.copy_in.transferred_bytes;
        const std::size_t prev_size = prev_header_size + prev_payload_size;
        const std::size_t payload_size = prev_payload_size >= buff.size()
                                             ? 0u
                                             : (std::min)(buff.size() - prev_payload_size, max_frame_size);
        const std::size_t total_size = prev_size + (payload_size == 0u ? 0u : payload_size + 5u);

        // Serialize what we need
        if (prev_header_size > 0u)
        {
            prev_frame_header = make_copy_data_header(exec_st.copy_in.frame_num_bytes);
            bufs.push_back(
                boost::capy::make_buffer(
                    std::span<const unsigned char>(prev_frame_header)
                        .subspan(exec_st.copy_in.transferred_bytes)
                )
            );
        }

        if (prev_payload_size > 0u)
        {
            bufs.push_back(boost::capy::make_buffer(buff_as_span.first(prev_payload_size)));
            buff_as_span = buff_as_span.subspan(prev_payload_size);
        }

        if (payload_size > 0u)
        {
            frame_header = make_copy_data_header(payload_size);
            bufs.push_back(boost::capy::make_buffer(frame_header));
            bufs.push_back(boost::capy::make_buffer(buff_as_span.first(payload_size)));
        }

        // Write the thing
        auto [ec, bytes] = co_await stream.write_some(std::span(bufs));
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
        // State check
        if (!exec_st.copy_in.active)
            co_return {client_errc::invalid_state};

        // If there are missing bytes to transfer, that's an error
        if (exec_st.copy_in.frame_num_bytes > 0u)
            co_return {client_errc::copy_in_incomplete_transfer};

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

capy::io_task<std::size_t> co_connection::write_some_copy_data(exec_state& st, capy::const_buffer buff)
{
    return impl_->write_some_copy_data(detail::exec_state_access::get_impl(st), buff);
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
