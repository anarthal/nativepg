//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_CHECK_HPP
#define NATIVEPG_CHECK_HPP

#include "nativepg/extended_error.hpp"
#include "nativepg/response_handler.hpp"
#include "nativepg/responses/detail/response_utils.hpp"

namespace nativepg {

// A response type that checks that no operation resulted in an error
class check
{
    extended_error err_;

public:
    check() = default;
    handler_setup_result setup(const request& req, std::size_t)
    {
        err_ = {};
        return req.messages().size();
    }
    void on_message(const any_request_message& msg, std::size_t) { detail::maybe_store_error(msg, err_); }
    const extended_error& result() const { return err_; }
};

// A response type that checks that a single resultset didn't
// produce an error, skipping any produced rows.
// May output a command_info structure containing data like affected rows
class check_execute
{
    command_info* info_{};
    extended_error err_;

public:
    check_execute() = default;
    check_execute(command_info& info) noexcept : info_(&info) {}

    handler_setup_result setup(const request& req, std::size_t offset)
    {
        if (info_)
            detail::reset_info(*info_);
        err_ = {};
        return detail::resultset_setup(req, offset);
    }
    void on_message(const any_request_message& msg, std::size_t);
    const extended_error& result() const { return err_; }
};

// A response that checks that a single parse (e.g. when preparing a statement)
// didn't produce an error
class check_parse
{
    extended_error err_;

public:
    check_parse() = default;

    handler_setup_result setup(const request& req, std::size_t offset);
    void on_message(const any_request_message& msg, std::size_t) { detail::maybe_store_error(msg, err_); }
    const extended_error& result() const { return err_; }
};

// A response that checks that a single parse (e.g. when preparing a statement)
// didn't produce an error
class check_bind
{
    extended_error err_;

public:
    check_bind() = default;

    handler_setup_result setup(const request& req, std::size_t offset);
    void on_message(const any_request_message& msg, std::size_t) { detail::maybe_store_error(msg, err_); }
    const extended_error& result() const { return err_; }
};

// A response that checks that a single close didn't produce an error
class check_close
{
    extended_error err_;

public:
    check_close() = default;

    handler_setup_result setup(const request& req, std::size_t offset);
    void on_message(const any_request_message& msg, std::size_t) { detail::maybe_store_error(msg, err_); }
    const extended_error& result() const { return err_; }
};

}  // namespace nativepg

#endif
