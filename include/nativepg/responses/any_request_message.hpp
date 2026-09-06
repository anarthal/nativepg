//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_ANY_REQUEST_MESSAGE_HPP
#define NATIVEPG_ANY_REQUEST_MESSAGE_HPP

#include <boost/assert.hpp>

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

// Contains any of the messages that a response handler may observe.
// Hand-rolled variant-like type.
class any_request_message
{
    [[noreturn]] static void throw_invalid_argument();

public:
    enum class kind
    {
        // Actual messages
        bind_complete,
        close_complete,
        command_complete,
        data_row,
        parameter_description,
        row_description,
        empty_query_response,
        portal_suspended,
        error_response,
        parse_complete,

        // Signals that the corresponding message was skipped due to a previous error
        message_skipped,
    };

    // Constructors from types (intentionally non-explicit)
    any_request_message(const protocol::bind_complete&) noexcept : kind_(kind::bind_complete), empty_{} {}
    any_request_message(const protocol::close_complete&) noexcept : kind_(kind::close_complete), empty_{} {}
    any_request_message(const protocol::command_complete& v) noexcept
        : kind_(kind::command_complete), command_complete_(v)
    {
    }
    any_request_message(const protocol::data_row& v) noexcept : kind_(kind::data_row), data_row_(v) {}
    any_request_message(const protocol::parameter_description& v) noexcept
        : kind_(kind::parameter_description), parameter_description_(v)
    {
    }
    any_request_message(const protocol::row_description& v) noexcept
        : kind_(kind::row_description), row_description_(v)
    {
    }
    any_request_message(const protocol::empty_query_response&) noexcept
        : kind_(kind::empty_query_response), empty_{}
    {
    }
    any_request_message(const protocol::portal_suspended&) noexcept : kind_(kind::portal_suspended), empty_{}
    {
    }
    any_request_message(const protocol::error_response& v) noexcept
        : kind_(kind::error_response), error_response_(v)
    {
    }
    any_request_message(const protocol::parse_complete&) noexcept : kind_(kind::parse_complete), empty_{} {}

    // Constructs a value with kind == message_skipped
    // Required because there is no message_skipped message in the protocol
    static any_request_message message_skipped() noexcept
    {
        return any_request_message(kind::message_skipped);
    }

    // Gets the kind
    kind type() const noexcept { return kind_; }

    // Getters. Only messages carrying data have them
    // Precondition: type() should match
    const protocol::command_complete& get_command_complete() const noexcept
    {
        BOOST_ASSERT(kind_ == kind::command_complete);
        return command_complete_;
    }
    const protocol::data_row& get_data_row() const noexcept
    {
        BOOST_ASSERT(kind_ == kind::data_row);
        return data_row_;
    }
    const protocol::parameter_description& get_parameter_description() const noexcept
    {
        BOOST_ASSERT(kind_ == kind::parameter_description);
        return parameter_description_;
    }
    const protocol::row_description& get_row_description() const noexcept
    {
        BOOST_ASSERT(kind_ == kind::row_description);
        return row_description_;
    }
    const protocol::error_response& get_error_response() const noexcept
    {
        BOOST_ASSERT(kind_ == kind::error_response);
        return error_response_;
    }

    // Checked getters. Throw if the actual kind doesn't match.
    const protocol::command_complete& as_command_complete() const
    {
        if (kind_ != kind::command_complete)
            throw_invalid_argument();
        return command_complete_;
    }
    const protocol::data_row& as_data_row() const
    {
        if (kind_ != kind::data_row)
            throw_invalid_argument();
        return data_row_;
    }
    const protocol::parameter_description& as_parameter_description() const
    {
        if (kind_ != kind::parameter_description)
            throw_invalid_argument();
        return parameter_description_;
    }
    const protocol::row_description& as_row_description() const
    {
        if (kind_ != kind::row_description)
            throw_invalid_argument();
        return row_description_;
    }
    const protocol::error_response& as_error_response() const
    {
        if (kind_ != kind::error_response)
            throw_invalid_argument();
        return error_response_;
    }

private:
    explicit any_request_message(kind k) noexcept : kind_(k), empty_{} {}

    kind kind_;
    union
    {
        unsigned char empty_;
        protocol::command_complete command_complete_;
        protocol::data_row data_row_;
        protocol::parameter_description parameter_description_;
        protocol::row_description row_description_;
        protocol::error_response error_response_;
    };
};

}  // namespace nativepg

#endif
