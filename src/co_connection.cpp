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
#include "nativepg/protocol/any_backend_message.hpp"
#include "nativepg/protocol/connection_state.hpp"
#include "nativepg/protocol/detail/connect_fsm.hpp"
#include "nativepg/protocol/detail/exec_fsm.hpp"
#include "nativepg/protocol/read_message_fsm.hpp"
#include "nativepg/protocol/read_response_fsm.hpp"
#include "nativepg/request.hpp"
#include "nativepg/response_handler.hpp"
#include "nativepg_internal/check_request.hpp"

namespace nativepg {

struct co_connection::impl
{
    boost::corosio::resolver resolv;
    boost::corosio::tcp_socket sock;
    protocol::connection_state st{};
    boost::capy::any_stream stream{&sock};

    // For operations spanning multiple functions
    enum class exec_some_status
    {
        idle,
        reading,
    };

    struct exec_some_state
    {
        exec_some_status status{exec_some_status::idle};
        std::optional<protocol::detail::read_response_fsm_impl> read_response_fsm;
        std::vector<boost::capy::const_buffer> copy_out;

        void reset()
        {
            status = exec_some_status::idle;
            read_response_fsm.reset();
            copy_out.clear();
        }
    } exec_some_st;

    explicit impl(boost::capy::execution_context& ctx) : resolv(ctx), sock(ctx) {}

    boost::capy::io_task<> physical_connect(const connect_params& params)
    {
        auto [ec, endpoints] = co_await resolv.resolve(params.hostname, std::to_string(params.port));
        if (ec)
            co_return {ec};

        auto [ec2, ep] = co_await boost::corosio::connect(sock, endpoints);
        co_return {ec2};
    }

    void setup_request(const request& req, response_handler_ref handler)
    {
        BOOST_ASSERT(exec_some_st.status == exec_some_status::idle);
        exec_some_st.read_response_fsm.emplace(&req, handler, true);
    }

    boost::capy::io_task<exec_some_result> exec_some()
    {
        BOOST_ASSERT(exec_some_st.read_response_fsm.has_value());
        auto& fsm = *exec_some_st.read_response_fsm;
        exec_some_st.copy_out.clear();

        // Write if required
        if (exec_some_st.status == exec_some_status::idle)
        {
            // Check that the request is valid
            if (auto req_ec = protocol::detail::setup_request(fsm.get_request(), fsm.get_handler()))
            {
                exec_some_st.reset();
                co_return {req_ec, {}};
            }

            // Write the request to the server
            auto [ec, size] = co_await boost::capy::write(
                stream,
                boost::capy::make_buffer(fsm.get_request().payload())
            );
            if (ec)
                co_return {ec, {}};

            exec_some_st.status = exec_some_status::reading;
        }

        // Read the response until we finished it, an error occurs, or a copy message is found
        auto act = st.read_msg_stream_fsm.resume(st, {}, 0u);

        while (true)
        {
            switch (act.type())
            {
                case protocol::read_message_stream_fsm::result_type::error: co_return {act.error(), {}};
                case protocol::read_message_stream_fsm::result_type::read:
                {
                    if (exec_some_st.copy_out.empty())
                    {
                        // No copy-related info, keep reading
                        auto [ec, bytes] = co_await stream.read_some(
                            boost::capy::make_buffer(act.read_buffer())
                        );
                        act = st.read_msg_stream_fsm.resume(st, ec, bytes);
                        break;
                    }
                    else
                    {
                        // We have received copy data during this iteration, return it
                        co_return {
                            {},
                            exec_some_result{exec_some_st.copy_out, false}
                        };
                    }
                }
                case protocol::read_message_stream_fsm::result_type::message:
                {
                    // Feed the protocol state machine. If we received an illegal message, we'll detect it
                    // here. Copy data never causes termination, so this is safe
                    auto res = fsm.resume(act.message());
                    if (res.type == protocol::detail::read_response_fsm_impl::result_type::done)
                    {
                        exec_some_st.reset();
                        co_return {res.ec, {}};
                    }

                    // React to copy messages
                    switch (act.message().type())
                    {
                        case protocol::any_backend_message::kind::copy_out_response:
                            // We're entering COPY OUT, notify the user
                            co_return {{}, exec_some_result{act.message().get_copy_out_response()}};
                        case protocol::any_backend_message::kind::copy_data:
                            // Store them, we'll return the entire batch when ready
                            exec_some_st.copy_out.push_back(
                                boost::capy::make_buffer(act.message().get_copy_data().data)
                            );
                            break;
                        case protocol::any_backend_message::kind::copy_done:
                        {
                            // The copy batch has finished, return the remaining messages and the eof
                            co_return {
                                {},
                                exec_some_result{exec_some_st.copy_out, true}
                            };
                        }
                        default: break;
                    }

                    // Attempt to read the next message
                    act = st.read_msg_stream_fsm.resume(st, {}, 0u);

                    break;
                }
            }
        }
    }
};

co_connection::co_connection(boost::capy::execution_context& ctx) : impl_(std::make_unique<impl>(ctx)) {}

co_connection& co_connection::operator=(co_connection&&) noexcept = default;

co_connection::~co_connection() = default;

// TODO: I'd prefer having connect_params be a view
// const references here may cause dangling parameters
boost::capy::io_task<> co_connection::connect(connect_params params, diagnostics* diag)
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
    exec_fsm fsm_(&req, handler);
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

void co_connection::setup_request(const request& req, response_handler_ref handler)
{
    return impl_->setup_request(req, handler);
}

boost::capy::io_task<exec_some_result> co_connection::exec_some() { return impl_->exec_some(); }

boost::capy::any_stream& co_connection::stream() { return impl_->stream; }

protocol::connection_state& co_connection::state() { return impl_->st; }

}  // namespace nativepg
