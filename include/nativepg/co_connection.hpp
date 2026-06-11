//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_CO_CONNECTION_HPP
#define NATIVEPG_CO_CONNECTION_HPP

#include <boost/capy/buffers.hpp>
#include <boost/capy/concept/executor.hpp>
#include <boost/capy/ex/execution_context.hpp>
#include <boost/capy/io/any_stream.hpp>
#include <boost/capy/io_task.hpp>

#include <concepts>
#include <memory>
#include <span>

#include "nativepg/connect_params.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/protocol/connection_state.hpp"
#include "nativepg/request.hpp"
#include "nativepg/response_handler.hpp"

namespace nativepg {

class exec_some_result
{
    enum class type
    {
        done,
        copy_out,
        copy_out_eof,
    };

    type type_{type::done};
    std::span<const boost::capy::const_buffer> copy_out;

public:
    exec_some_result() noexcept = default;
    exec_some_result(std::span<const boost::capy::const_buffer> copy_out_buffers, bool is_eof) noexcept
        : type_(is_eof ? type::copy_out_eof : type::copy_out), copy_out(copy_out_buffers)
    {
    }

    bool is_done() const { return type_ == type::done; }
    bool is_copy_out() const { return type_ == type::copy_out || type_ == type::copy_out_eof; }
    bool is_copy_out_eof() const { return type_ == type::copy_out_eof; }
    std::span<const boost::capy::const_buffer> copy_out_data() const { return copy_out; }
};

class co_connection
{
    struct impl;
    std::unique_ptr<impl> impl_;

public:
    explicit co_connection(boost::capy::execution_context& ctx);

    template <class Ex>
        requires(!std::same_as<Ex, co_connection> && boost::capy::Executor<Ex>)
    explicit co_connection(const Ex& ex) : co_connection{ex.context()}
    {
    }

    co_connection(co_connection&&) = default;
    co_connection(const co_connection&) = delete;

    co_connection& operator=(co_connection&&) noexcept;
    co_connection& operator=(const co_connection&) = delete;

    ~co_connection();

    boost::capy::io_task<> connect(connect_params params, diagnostics* diag = nullptr);

    boost::capy::io_task<> exec(
        const request& req,
        response_handler_ref handler,
        diagnostics* diag = nullptr
    );

    // TODO: I think this could be more ergonomic if we packaged the request and handler
    // into a generator-like object
    boost::capy::io_task<exec_some_result> exec_some(
        const request& req,
        response_handler_ref handler,
        diagnostics* diag = nullptr
    );

    // TODO: I don't like this
    boost::capy::any_stream& stream();
    protocol::connection_state& state();
};

}  // namespace nativepg

#endif
