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
#include <boost/corosio/tcp_socket.hpp>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "nativepg/co_connection.hpp"
#include "nativepg/connect_params.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/protocol/connection_state.hpp"
#include "nativepg/protocol/detail/connect_fsm.hpp"
#include "nativepg/protocol/detail/exec_fsm.hpp"
#include "nativepg/protocol/detail/exec_some_fsm.hpp"
#include "nativepg/protocol/parse_message.hpp"
#include "nativepg/request.hpp"
#include "nativepg/response.hpp"
#include "nativepg/response_handler.hpp"

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

    explicit impl(capy::execution_context& ctx) : resolv(ctx), sock(ctx) {}

    capy::io_task<> physical_connect(const connect_params& params)
    {
        auto [ec, endpoints] = co_await resolv.resolve(params.hostname, std::to_string(params.port));
        if (ec)
            co_return {ec};

        auto [ec2, ep] = co_await boost::corosio::connect(sock, endpoints);
        co_return {ec2};
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

capy::io_task<> co_connection::exec(const request& req, response_handler_ref handler, diagnostics* diag)
{
    using protocol::detail::exec_fsm;

    // Initialize
    exec_fsm fsm_(&req, handler);
    auto res = fsm_.resume(impl_->st, {}, 0u);

    while (true)
    {
        switch (res.type())
        {
            case protocol::startup_fsm::result_type::write:
            {
                auto [ec, bytes] = co_await capy::write(impl_->sock, capy::make_buffer(res.write_data()));
                res = fsm_.resume(impl_->st, ec, bytes);
                break;
            }
            case protocol::startup_fsm::result_type::read:
            {
                auto [ec, bytes] = co_await impl_->sock.read_some(capy::make_buffer(res.read_buffer()));
                res = fsm_.resume(impl_->st, ec, bytes);
                break;
            }
            case protocol::startup_fsm::result_type::done:
            {
                auto result = fsm_.get_result(res.error());
                if (diag)
                    *diag = std::move(result.diag);
                co_return {result.code};
            }
            default: BOOST_ASSERT(false); co_return {};
        }
    }
}

void co_connection::setup_request(const request& req, response_handler_ref handler)
{
    return impl_->setup_request(req, handler);
}

capy::io_task<exec_some_result> co_connection::exec_some() { return impl_->exec_some(); }

capy::io_task<> co_connection::read_some_messages() { return impl_->read_some_messages(); }

capy::any_stream& co_connection::stream() { return impl_->stream; }

protocol::connection_state& co_connection::state() { return impl_->st; }

}  // namespace nativepg
