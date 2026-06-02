//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/assert.hpp>
#include <boost/capy/buffers/make_buffer.hpp>
#include <boost/capy/ex/execution_context.hpp>
#include <boost/capy/io_task.hpp>
#include <boost/capy/write.hpp>
#include <boost/corosio/connect.hpp>
#include <boost/corosio/resolver.hpp>
#include <boost/corosio/tcp_socket.hpp>

#include <memory>
#include <string>

#include "nativepg/co_connection.hpp"
#include "nativepg/connect_params.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/protocol/connection_state.hpp"
#include "nativepg/protocol/detail/connect_fsm.hpp"
#include "nativepg/protocol/detail/exec_fsm.hpp"
#include "nativepg/request.hpp"
#include "nativepg/response_handler.hpp"

namespace nativepg {

struct co_connection::impl
{
    boost::corosio::resolver resolv;
    boost::corosio::tcp_socket sock;
    protocol::connection_state st{};

    explicit impl(boost::capy::execution_context& ctx) : resolv(ctx), sock(ctx) {}

    boost::capy::io_task<> physical_connect(const connect_params& params)
    {
        auto [ec, endpoints] = co_await resolv.resolve(params.hostname, std::to_string(params.port));
        if (ec)
            co_return {ec};

        auto [ec2, ep] = co_await boost::corosio::connect(sock, endpoints);
        co_return {ec2};
    }
};

co_connection::co_connection(boost::capy::execution_context& ctx) : impl_(std::make_unique<impl>(ctx)) {}

co_connection::~co_connection() = default;

boost::capy::io_task<> co_connection::connect(const connect_params& params, diagnostics* diag)
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
                auto [ec, bytes] = co_await boost::capy::write(
                    impl_->sock,
                    boost::capy::make_buffer(res.write_data())
                );
                res = fsm_.resume(impl_->st, ec, bytes);
                break;
            }
            case connect_fsm::result_type::read:
            {
                auto [ec, bytes] = co_await impl_->sock.read_some(
                    boost::capy::make_buffer(res.read_buffer())
                );
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

boost::capy::io_task<> co_connection::exec(
    const request& req,
    response_handler_ref handler,
    diagnostics* diag
)
{
    using protocol::detail::exec_fsm;

    // Initialize
    exec_fsm fsm_(req, handler);
    auto res = fsm_.resume(impl_->st, {}, 0u);

    while (true)
    {
        switch (res.type())
        {
            case protocol::startup_fsm::result_type::write:
            {
                auto [ec, bytes] = co_await boost::capy::write(
                    impl_->sock,
                    boost::capy::make_buffer(res.write_data())
                );
                res = fsm_.resume(impl_->st, ec, bytes);
                break;
            }
            case protocol::startup_fsm::result_type::read:
            {
                auto [ec, bytes] = co_await impl_->sock.read_some(
                    boost::capy::make_buffer(res.read_buffer())
                );
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

}  // namespace nativepg
