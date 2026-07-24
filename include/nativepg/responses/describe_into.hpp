//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_DESCRIBE_INTO_HPP
#define NATIVEPG_DESCRIBE_INTO_HPP

#include "nativepg/extended_error.hpp"
#include "nativepg/responses/field_descriptions.hpp"
#include "nativepg/responses/response_handler.hpp"

namespace nativepg {

class describe_into
{
    field_descriptions* obj_;
    extended_error err_;

public:
    describe_into(field_descriptions& obj) noexcept : obj_(&obj) {}

    handler_setup_result setup(const request& req, std::size_t offset);
    void on_message(const any_request_message& msg, std::size_t);
    const extended_error& result() const { return err_; }
};

}  // namespace nativepg

#endif
