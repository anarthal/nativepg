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
#include "nativepg/protocol/copy.hpp"
#include "nativepg/protocol/startup_fsm.hpp"
#include "nativepg/request.hpp"
#include "nativepg/response_handler.hpp"

namespace nativepg {

class exec_some_result
{
public:
    enum class kind
    {
        copy_out,
        copy_out_data,
        done,
    };

    exec_some_result() = default;
    exec_some_result(protocol::copy_out_response value) noexcept : type_(kind::copy_out), data_(value) {}
    exec_some_result(std::span<const boost::capy::const_buffer> value, bool eof) noexcept
        : type_(kind::copy_out_data), data_({value, eof})
    {
    }

    kind type() const { return type_; }
    protocol::copy_out_response get_copy_out() const
    {
        BOOST_ASSERT(type_ == kind::copy_out);
        return data_.copy_out;
    }
    std::span<const boost::capy::const_buffer> get_copy_out_data() const
    {
        BOOST_ASSERT(type_ == kind::copy_out_data);
        return data_.copy_out_data.buffers;
    }
    bool get_copy_out_eof() const
    {
        BOOST_ASSERT(type_ == kind::copy_out_data);
        return data_.copy_out_data.is_eof;
    }

private:
    kind type_{kind::done};
    struct copy_out_data_t
    {
        std::span<const boost::capy::const_buffer> buffers;
        bool is_eof;
    };

    union data_t
    {
        protocol::copy_out_response copy_out;
        copy_out_data_t copy_out_data;

        data_t() : copy_out_data() {}
        data_t(protocol::copy_out_response value) noexcept : copy_out(value) {}
        data_t(copy_out_data_t value) noexcept : copy_out_data(value) {}
    } data_;
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

    // The request and the handler must live until the entire response has been read
    // with exec_some
    void setup_request(const request& req, response_handler_ref handler);
    boost::capy::io_task<exec_some_result> exec_some();

    // TODO: I don't like this
    boost::capy::any_stream& stream();
    protocol::connection_state& state();
};

}  // namespace nativepg

#endif
