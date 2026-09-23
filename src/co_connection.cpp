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

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "nativepg/client_errc.hpp"
#include "nativepg/co_connection.hpp"
#include "nativepg/connect_params.hpp"
#include "nativepg/encoding.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/protocol/async.hpp"
#include "nativepg/protocol/connection_state.hpp"
#include "nativepg/protocol/detail/connect_fsm.hpp"
#include "nativepg/protocol/detail/exec_some_fsm.hpp"
#include "nativepg/protocol/parse_message.hpp"
#include "nativepg/protocol/terminate.hpp"
#include "nativepg/request.hpp"
#include "nativepg/responses/response_handler.hpp"
#include "nativepg_internal/multiplexed_connection/multiplexer_v2.hpp"
#include "nativepg_internal/multiplexed_connection/notification_store.hpp"

namespace capy = boost::capy;
namespace corosio = boost::corosio;

namespace nativepg {

struct co_connection::impl
{
    corosio::resolver resolv;
    corosio::tcp_socket sock;
    protocol::connection_state st{};
    capy::any_stream stream{&sock};
    std::vector<capy::const_buffer> copy_out_buffers;
    std::optional<protocol::detail::exec_some_fsm> exec_some_fsm;
    detail::multiplexer_v2 mpx_;  // TODO: clean up this?
    detail::notification_store exec_notifications_, receive_notifications_;
    bool receiver_running_{};
    std::error_code receiver_pending_ec_{};

    void reset()
    {
        // TODO: this somehow conflicts with connection_state::reset()
        // For now we keep both, as this is specific to co_connection, but the ideal
        // is having just one
        exec_notifications_.clear();
        exec_notifications_.set_deep(true);
        receive_notifications_.clear();
        receiver_pending_ec_ = {};
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

    // This is the writer side of exec
    boost::capy::io_task<> write_request(detail::multiplexer_v2::write_guard guard, const request& req)
    {
        // Write any potential leftover from previous requests, plus our own request.
        // The former is required to keep the connection healthy.
        // Most of the time, the 1st buffer is empty, and Corosio coalesces this to a
        // non-vectored write, for all backends.
        auto [ec, bytes_written] = co_await boost::capy::write(
            stream,
            std::array<boost::capy::const_buffer, 2u>{
                boost::capy::make_buffer(guard.previous_write_bytes()),
                boost::capy::make_buffer(req.payload())
            }
        );

        // Report the result, so subsequent execs know how to keep the connection healthy
        std::move(guard).report_result(bytes_written);

        // Done
        co_return {ec};
    }

    boost::capy::io_task<> read_response(
        detail::multiplexer_v2::read_guard guard,
        const request& req,
        response_handler_ref handler,
        diagnostics* diag
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
                    if (auto [ec] = co_await read_some_messages(); ec)
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

            // Store notifications so the receive loop can return them
            if (res.message.type() == protocol::any_backend_message::kind::notification_response)
            {
                exec_notifications_.push_back(res.message.get_notification_response());
                mpx_.notify_receiver();
            }

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
                    std::move(guard).report_success();
                    if (diag)
                        *diag = fsm.get_handler_error().diag;  // TODO: could we move assign?
                    co_return {fsm.get_handler_error().code};
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

    boost::capy::io_task<> exec(const request& req, response_handler_ref handler, diagnostics* diag = nullptr)
    {
        // Perform request setup
        if (auto ec = protocol::detail::setup_request(req, handler))
            co_return {ec};

        // Wait for our turn to write and register what we are doing in the queue
        detail::multiplexer_v2::task_node node;
        auto [enter_ec, write_guard, read_guard] = co_await mpx_.enter(node, &req);
        if (enter_ec)
            co_return {enter_ec};

        // Run the reader and writer tasks in parallel
        // TODO: protocol violations should mark the connection as failed
        // once we have state checks
        auto [final_ec, writer_dummy, reader_dummy] = co_await boost::capy::when_all(
            write_request(std::move(write_guard), req),
            read_response(std::move(read_guard), req, handler, diag)
        );

        co_return {final_ec};
    }

    boost::capy::io_task<std::span<const protocol::notification_response>> receive()
    {
        struct receiver_running_deleter
        {
            void operator()(co_connection::impl* p) const { p->receiver_running_ = false; }
        };

        // Verify that no two receivers run in parallel
        if (receiver_running_)
            co_return {client_errc::already_running, {}};
        receiver_running_ = true;
        std::unique_ptr<co_connection::impl, receiver_running_deleter> receiver_running_guard{this};

        // If there is a pending error, return it
        if (auto pending_ec = std::exchange(receiver_pending_ec_, std::error_code()))
            co_return {pending_ec, {}};

        // Wait for notifications to arrive/be read
        auto [ec] = co_await wait_for_notifications();

        // If we managed to read any, return them and queue the error
        if (!receive_notifications_.get().empty())
        {
            receiver_pending_ec_ = ec;
            co_return {{}, receive_notifications_.get()};
        }

        // This should be an error
        BOOST_ASSERT(ec);
        co_return {ec, {}};
    }

    boost::capy::io_task<> wait_for_notifications()
    {
        while (true)
        {
            // Wait for either notifications to arrive, or for our turn to read
            auto [ec, guard] = co_await mpx_.enter_receive();
            if (ec)
                co_return {ec};

            // Look for cached notifications first
            if (!exec_notifications_.get().empty())
            {
                std::swap(exec_notifications_, receive_notifications_);
                exec_notifications_.clear();
                exec_notifications_.set_deep(true);
                co_return {};
            }

            // Is it our turn to read? At this point, it probably is,
            // but race conditions with other exec()s could make it not the case
            if (guard.is_reading())
            {
                receive_notifications_.clear();
                receive_notifications_.set_deep(false);
                auto [loop_ec] = co_await receive_read(guard);
                if (loop_ec || !receive_notifications_.get().empty())
                    co_return {loop_ec};
            }

            // We yielded because we received a message that wasn't for us,
            // but we don't have anything to report
        }
    }

    boost::capy::io_task<> receive_read(detail::multiplexer_v2::receive_guard& guard)
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
                    if (!receive_notifications_.get().empty())
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
                    BOOST_ASSERT(receive_notifications_.is_deep());
                    receive_notifications_.push_back(res.message.get_notification_response());
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
                        co_return {};
                    }
            }
        }
    }

    void setup_request(const request& req, response_handler_ref handler)
    {
        BOOST_ASSERT(!exec_some_fsm.has_value());
        exec_some_fsm.emplace(&req, handler);
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

    capy::io_task<exec_some_result> exec_some()
    {
        BOOST_ASSERT(exec_some_fsm.has_value());
        auto& fsm = *exec_some_fsm;

        while (true)
        {
            auto act = fsm.resume(st, copy_out_buffers);

            switch (act.type())
            {
                case protocol::detail::exec_some_fsm::result_type::write:
                {
                    auto [ec, bytes] = co_await capy::write(
                        stream,
                        capy::make_buffer(fsm.get_request().payload())
                    );
                    if (ec)
                        co_return {ec, {}};
                    break;
                }
                case protocol::detail::exec_some_fsm::result_type::read:
                {
                    auto [ec] = co_await read_some_messages();
                    if (ec)
                        co_return {ec, {}};
                    break;
                }
                case protocol::detail::exec_some_fsm::result_type::copy_out:
                {
                    co_return {{}, exec_some_result{act.get_copy_out()}};
                }
                case protocol::detail::exec_some_fsm::result_type::copy_data:
                {
                    co_return {
                        {},
                        exec_some_result{copy_out_buffers, false}
                    };
                }
                case protocol::detail::exec_some_fsm::result_type::copy_data_with_eof:
                {
                    co_return {
                        {},
                        exec_some_result{copy_out_buffers, true}
                    };
                }
                case protocol::detail::exec_some_fsm::result_type::done:
                {
                    exec_some_fsm.reset();
                    co_return {act.error(), {}};
                }
            }
        }
    }
};

co_connection::co_connection(capy::execution_context& ctx) : impl_(std::make_unique<impl>(ctx)) {}

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

capy::io_task<> co_connection::exec(const request& req, response_handler_ref handler, diagnostics* diag)
{
    return impl_->exec(req, handler, diag);
}

capy::io_task<std::span<const protocol::notification_response>> co_connection::receive()
{
    return impl_->receive();
}

void co_connection::setup_request(const request& req, response_handler_ref handler)
{
    // TODO: exec_some() currently plays badly with multiplexing
    return impl_->setup_request(req, handler);
}

capy::io_task<exec_some_result> co_connection::exec_some() { return impl_->exec_some(); }

capy::io_task<> co_connection::read_some_messages() { return impl_->read_some_messages(); }

capy::any_stream& co_connection::stream() { return impl_->stream; }

protocol::connection_state& co_connection::state() { return impl_->st; }

std::optional<bool> co_connection::standard_conforming_strings() const
{
    return impl_->st.standard_conforming_strings;
}

std::optional<encoding> co_connection::client_encoding() const { return impl_->st.client_encoding; }

}  // namespace nativepg
