//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_CONNECTION_HPP
#define NATIVEPG_CONNECTION_HPP

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/async_result.hpp>
#include <boost/asio/compose.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/consign.hpp>
#include <boost/asio/deferred.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/write.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/as_tuple.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <cstddef>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

#include "nativepg/connect_params.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/protocol/connection_state.hpp"
#include "nativepg/protocol/detail/connect_fsm.hpp"
#include "nativepg/protocol/detail/exec_fsm.hpp"
#include "nativepg/protocol/startup_fsm.hpp"
#include "nativepg/request.hpp"
#include "nativepg/response_handler.hpp"
#include "nativepg/context/context.hpp"

namespace nativepg {

namespace detail {

struct connection_impl
{
    boost::asio::ip::tcp::resolver resolv;
    boost::asio::ip::tcp::socket sock;
    protocol::connection_state st{};

    explicit connection_impl(boost::asio::any_io_executor ex) : resolv(ex), sock(std::move(ex)) {}
};

struct physical_connect_op
{
    connection_impl& impl;
    const connect_params& params_;

    template <class Self>
    void operator()(Self& self)
    {
        impl.resolv.async_resolve(params_.hostname, std::to_string(params_.port), std::move(self));
    }

    template <class Self>
    void operator()(
        Self& self,
        boost::system::error_code ec,
        boost::asio::ip::tcp::resolver::results_type results
    )
    {
        if (ec)
            self.complete(ec);
        boost::asio::async_connect(impl.sock, results, std::move(self));
    }

    template <class Self>
    void operator()(Self& self, boost::system::error_code ec, boost::asio::ip::tcp::endpoint)
    {
        self.complete(ec);
    }
};

template <boost::asio::completion_token_for<void(boost::system::error_code)> CompletionToken>
auto async_physical_connect(connection_impl& conn, const connect_params& params, CompletionToken&& token)
{
    return boost::asio::async_compose<CompletionToken, void(boost::system::error_code)>(
        physical_connect_op{conn, params},
        token,
        conn.sock
    );
}

struct connect_op
{
    connection_impl& impl;
    protocol::detail::connect_fsm fsm_;

    template <class Self>
    void operator()(Self& self, boost::system::error_code ec = {}, std::size_t bytes_transferred = {})
    {
        using protocol::detail::connect_fsm;

        auto res = fsm_.resume(impl.st, ec, bytes_transferred);
        switch (res.type())
        {
            case connect_fsm::result_type::write:
                boost::asio::async_write(impl.sock, res.write_data(), std::move(self));
                break;
            case connect_fsm::result_type::read:
                impl.sock.async_read_some(res.read_buffer(), std::move(self));
                break;
            case connect_fsm::result_type::connect:
                async_physical_connect(impl, fsm_.params(), std::move(self));
                break;
            case connect_fsm::result_type::close:
                ec = impl.sock.close(ec);
                (*this)(self, ec);
                break;
            case connect_fsm::result_type::done:
                self.complete(extended_error{res.error(), impl.st.shared_diag});
                break;
            default: BOOST_ASSERT(false);
        }
    }
};

struct exec_op
{
    connection_impl& impl;
    protocol::detail::exec_fsm fsm_;

    template <class Self>
    void operator()(Self& self, boost::system::error_code ec = {}, std::size_t bytes_transferred = {})
    {
        auto res = fsm_.resume(impl.st, ec, bytes_transferred);
        switch (res.type())
        {
            case protocol::startup_fsm::result_type::write:
                boost::asio::async_write(impl.sock, res.write_data(), std::move(self));
                break;
            case protocol::startup_fsm::result_type::read:
                impl.sock.async_read_some(res.read_buffer(), std::move(self));
                break;
            case protocol::startup_fsm::result_type::done: self.complete(fsm_.get_result(res.error())); break;
            default: BOOST_ASSERT(false);
        }
    }
};

}  // namespace detail

class connection
{
    std::unique_ptr<detail::connection_impl> impl_;
    context::context ctx_;

public:
    explicit connection(boost::asio::any_io_executor ex) : impl_(new detail::connection_impl{std::move(ex)})
    {
    }
    // TODO: ctor from execution context

    boost::asio::any_io_executor get_executor() { return impl_->sock.get_executor(); }

    template <
        boost::asio::completion_token_for<void(extended_error)> CompletionToken = boost::asio::deferred_t>
    auto async_connect(const connect_params& params, CompletionToken&& token = {})
    {
        return boost::asio::async_compose<CompletionToken, void(extended_error)>(
            detail::connect_op{*impl_, protocol::detail::connect_fsm{params}},
            token,
            impl_->sock
        );
    }

    template <
        boost::asio::completion_token_for<void(extended_error)> CompletionToken = boost::asio::deferred_t>
    auto async_exec(const request& req, response_handler_ref handler, CompletionToken&& token = {})
    {
        return boost::asio::async_compose<CompletionToken, void(extended_error)>(
            detail::exec_op{
                *impl_,
                protocol::detail::exec_fsm{&req, handler}
        },
            token,
            impl_->sock
        );
    }

    template <
        response_handler ResponseHandler,
        boost::asio::completion_token_for<void(extended_error)> CompletionToken = boost::asio::deferred_t>
    auto async_exec(const request& req, ResponseHandler&& handler, CompletionToken&& token = {})
    {
        // TODO: use associated allocator
        auto ptr = std::make_unique<std::decay_t<ResponseHandler>>(std::forward<ResponseHandler>(handler));
        response_handler_ref href{ptr.get()};
        return async_exec(
            req,
            href,
            boost::asio::consign(std::forward<CompletionToken>(token), std::move(ptr))
        );
    }

    template <context::ValidStateTag... Tags, typename CompletionToken = boost::asio::deferred_t>
    [[nodiscard]] auto async_load_contexts(CompletionToken&& token = {}) noexcept {
        // 1. Initiate a standard asynchronous composed operation using Asio's helper
        return boost::asio::async_initiate<CompletionToken, void(extended_error)>(
            [this](auto handler) {
                // Get the executor from your underlying connection or target token handler
                auto exec = boost::asio::get_associated_executor(handler, this->get_executor());

                // 2. Spawn the internal coroutine helper onto the active executor pipeline
                boost::asio::co_spawn(exec,
                    [this]() -> boost::asio::awaitable<extended_error> {
                        co_return co_await this->impl_async_load_contexts<Tags...>();
                    },
                    [moved_handler = std::move(handler)](std::exception_ptr ep, extended_error err) mutable {
                        if (ep) {
                            // Safe fallback if an unexpected runtime breakdown happens
                            moved_handler(extended_error{client_errc::unexpected_message});
                        } else {
                            // Pass your structured error code straight back to the token line
                            moved_handler(err);
                        }
                    }
                );
            },
            token
        );
    }

    context::context& context() noexcept { return ctx_; }

private:
    template <context::ValidStateTag... Tags>
    [[nodiscard]] boost::asio::awaitable<extended_error> impl_async_load_contexts() noexcept {
        context::query_collector collector{};

        // Collect phase via unrolled compile-time lambda folding
        auto collect_loader = [&collector](auto tag_identity) {
            using Tag = typename decltype(tag_identity)::type;
            using Loader = context::context_state_loader<Tag>;

            collector.add_query(
                Loader::get_query(),
                [](const resultset_view& result, diagnostics* diag) noexcept -> std::unique_ptr<context::context_state> {
                    return Loader::parse(result, diag);
                }
            );
        };
        (collect_loader(std::type_identity<Tags>{}), ...);

        resultsets res{};

        auto [err] = co_await async_exec(
            collector.combined_queries,
            resultsets_handler{res},
            boost::asio::as_tuple
        );

        // Check if the query execution returned a database or network failure
        if (err.code) {
            co_return err;
        }

        // Check if the number of result sets matches our queries
        if (res.size() != collector.parsers.size()) {
            co_return extended_error{client_errc::unexpected_message};
        }

        // Hydration loop driving out-parameter state updates
        for (size_t i = 0; i < collector.parsers.size(); ++i) {
            diagnostics diag{};
            std::unique_ptr<context::context_state> state = collector.parsers[i](res[i], &diag);

            if (state) {
                ctx_.commit_state(std::move(state));
            } else {
                co_return extended_error{client_errc::incomplete_message};
            }
        }

        co_return extended_error{}; // Return clean success code
    }
};

}  // namespace nativepg

#endif
