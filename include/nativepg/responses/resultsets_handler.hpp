//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_RESULTSETS_HPP
#define NATIVEPG_RESULTSETS_HPP

#include <cstddef>

#include "nativepg/dynamic_resultset.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/responses/response_handler.hpp"

namespace nativepg {

class resultsets_handler
{
    enum class state_t
    {
        parsing_meta,
        parsing_data,
    };

    resultsets* obj_;
    extended_error err_;
    state_t state_{state_t::parsing_meta};
    std::size_t num_cols_{};
    std::size_t num_rows_{};

    void reset_state()
    {
        state_ = state_t::parsing_meta;
        num_cols_ = 0u;
        num_rows_ = 0u;
    }

public:
    resultsets_handler(resultsets& r) noexcept : obj_(&r) {}

    handler_setup_result setup(const request& req, std::size_t offset);
    void on_message(const any_request_message& msg, std::size_t);
    const extended_error& result() const { return err_; }
};

}  // namespace nativepg

#endif
