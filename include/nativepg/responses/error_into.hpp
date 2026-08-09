//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_ERROR_INTO_HPP
#define NATIVEPG_ERROR_INTO_HPP

#include <concepts>
#include <cstddef>
#include <type_traits>
#include <utility>

#include "nativepg/extended_error.hpp"
#include "nativepg/responses/response_handler.hpp"

namespace nativepg {

// Wrapper response handler that allows extracting individual errors.
// Use it when executing a pipeline with multiple, independent steps,
// some of which may fail independently of the others.
template <response_handler Handler>
class error_into
{
    Handler inner_;
    extended_error* err_ptr_;

public:
    template <class H>
        requires std::constructible_from<Handler, H&&>
    error_into(H&& handler, extended_error& err) : inner_(std::forward<H>(handler)), err_ptr_(&err)
    {
    }

    handler_setup_result setup(const request& req, std::size_t offset)
    {
        *err_ptr_ = {};
        return inner_.setup(req, offset);
    }

    void on_message(const any_request_message& msg, std::size_t offset, extended_error& err)
    {
        inner_.on_message(msg, offset, err);
        if (err.code && !err_ptr_->code)
            *err_ptr_ = err;
    }

    // Access the inner handler
    Handler& handler() noexcept { return inner_; }
    const Handler& handler() const noexcept { return inner_; }
};

template <class H>
error_into(H&&, extended_error&) -> error_into<std::decay_t<H>>;

}  // namespace nativepg

#endif
