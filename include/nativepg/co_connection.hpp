//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_CO_CONNECTION_HPP
#define NATIVEPG_CO_CONNECTION_HPP

#include <boost/capy/concept/executor.hpp>
#include <boost/capy/ex/execution_context.hpp>
#include <boost/capy/io_task.hpp>

#include <concepts>
#include <memory>

#include "nativepg/connect_params.hpp"
#include "nativepg/request.hpp"
#include "nativepg/response_handler.hpp"

namespace nativepg {

class co_connection
{
    struct impl;
    std::unique_ptr<impl> impl_;

public:
    explicit co_connection(boost::capy::execution_context& ctx);

    template <boost::capy::Executor Ex>
        requires(!std::same_as<Ex, co_connection>)
    explicit co_connection(const Ex& ex) : co_connection{ex.context()}
    {
    }

    ~co_connection();

    boost::capy::io_task<> connect(const connect_params& params);

    boost::capy::io_task<> exec(const request& req, response_handler_ref handler);
};

}  // namespace nativepg

#endif
