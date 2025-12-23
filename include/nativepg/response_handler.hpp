//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_RESPONSE_HANDLER_HPP
#define NATIVEPG_RESPONSE_HANDLER_HPP

#include <boost/compat/function_ref.hpp>
#include <boost/system/error_code.hpp>
#include <boost/variant2/variant.hpp>

#include "nativepg/protocol/bind.hpp"
#include "nativepg/protocol/close.hpp"
#include "nativepg/protocol/command_complete.hpp"
#include "nativepg/protocol/data_row.hpp"
#include "nativepg/protocol/describe.hpp"
#include "nativepg/protocol/empty_query_response.hpp"
#include "nativepg/protocol/execute.hpp"
#include "nativepg/protocol/notice_error.hpp"
#include "nativepg/protocol/parse.hpp"

namespace nativepg {

class diagnostics;

// TODO: maybe make this a class
using any_request_message = boost::variant2::variant<
    protocol::bind_complete,
    protocol::close_complete,
    protocol::command_complete,
    protocol::data_row,
    protocol::parameter_description,
    protocol::row_description,
    protocol::no_data,
    protocol::empty_query_response,
    protocol::portal_suspended,
    protocol::error_response,
    protocol::parse_complete>;

using response_handler_ref = boost::compat::function_ref<
    boost::system::error_code(const any_request_message&, diagnostics&)>;

template <class T>
concept response_handler = requires(T& handler, const any_request_message& msg, diagnostics& diag) {
    { handler(msg, diag) } -> std::convertible_to<boost::system::error_code>;
};

}  // namespace nativepg

#endif
