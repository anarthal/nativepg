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
#include <string>
#include <vector>

#include "nativepg/co_connection.hpp"
#include "nativepg/connect_params.hpp"
#include "nativepg/encoding.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/protocol/connection_state.hpp"
#include "nativepg/protocol/detail/connect_fsm.hpp"
#include "nativepg/protocol/detail/exec_some_fsm.hpp"
#include "nativepg/protocol/parse_message.hpp"
#include "nativepg/protocol/terminate.hpp"
#include "nativepg/request.hpp"
#include "nativepg/responses/response_handler.hpp"
#include "nativepg_internal/multiplexed_connection/multiplexer_v2.hpp"

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
        auto [final_ec, writer_dummy, reader_dummy] = co_await boost::capy::when_all(
            write_request(std::move(write_guard), req),
            read_response(std::move(read_guard), req, handler, diag)
        );

        co_return {final_ec};
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

void co_connection::setup_request(const request& req, response_handler_ref handler)
{
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
