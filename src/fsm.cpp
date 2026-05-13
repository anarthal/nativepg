//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/asio/buffer.hpp>
#include <boost/system/detail/error_code.hpp>
#include <boost/system/system_error.hpp>
#include <boost/variant2/variant.hpp>

#include <cstddef>
#include <span>

#include "coroutine.hpp"
#include "nativepg/client_errc.hpp"
#include "nativepg/protocol/connection_state.hpp"
#include "nativepg/protocol/detail/connect_fsm.hpp"
#include "nativepg/protocol/detail/exec_fsm.hpp"
#include "nativepg/protocol/header.hpp"
#include "nativepg/protocol/messages.hpp"
#include "nativepg/protocol/read_message_fsm.hpp"
#include "nativepg/protocol/read_response_fsm.hpp"
#include "nativepg/protocol/startup_fsm.hpp"
#include "nativepg/request.hpp"
#include "nativepg/response_handler.hpp"

using namespace nativepg::protocol;
using boost::system::error_code;
using detail::connect_fsm;
using detail::exec_fsm;
using nativepg::client_errc;

read_message_fsm::result read_message_fsm::resume(std::span<const unsigned char> data)
{
    if (msg_size_ == -1)
    {
        // Header. Ensure we have enough data
        if (data.size() < 5u)
            return result(static_cast<std::size_t>(5u - data.size()));

        // Do the parsing
        auto header_result = parse_header(boost::span<const unsigned char, 5>(data));
        if (header_result.has_error())
            return header_result.error();

        // Record the header fields to signal that we're done with the header
        auto header = *header_result;
        msg_type_ = header.type;
        msg_size_ = header.size;
    }

    // Body. Ensure we have enough data. The header is not discarded
    // until the message is fully parsed for simplicity,
    // and the type byte is not included in the length
    BOOST_ASSERT(msg_size_ != -1);
    const auto expected_size = static_cast<std::size_t>(msg_size_ + 1);
    if (data.size() < expected_size)
        return result(static_cast<std::size_t>(expected_size - data.size()));

    // Do the parsing
    auto msg_result = parse(msg_type_, data.subspan(5, expected_size - 5));
    if (msg_result.has_error())
        return msg_result.error();
    return result(*msg_result, expected_size);
}

static std::span<unsigned char> to_span(boost::asio::mutable_buffer buff)
{
    return {static_cast<unsigned char*>(buff.data()), buff.size()};
}

read_message_stream_fsm::result read_message_stream_fsm::resume(
    connection_state& st,
    boost::system::error_code io_ec,
    std::size_t bytes_read
)
{
    // TODO: we could likely rearrange this code to avoid these temporaries and have less state
    read_message_fsm::result res{error_code()};
    switch (resume_point_)
    {
        NATIVEPG_CORO_INITIAL

        while (true)
        {
            res = fsm_.resume(to_span(st.read_buffer.data()));

            if (res.type() == read_message_fsm::result_type::error)
            {
                // An error is always fatal
                return res.error();
            }
            else if (res.type() == read_message_fsm::result_type::message)
            {
                // We have a message. Yield it and then consume the used bytes
                bytes_to_consume_ = res.bytes_consumed();
                NATIVEPG_YIELD(resume_point_, 1, res.message());
                st.read_buffer.consume(bytes_to_consume_);
                fsm_ = {};
            }
            else
            {
                BOOST_ASSERT(res.type() == read_message_fsm::result_type::needs_more);

                // Prepare the buffer and tell the caller to read
                NATIVEPG_YIELD(resume_point_, 2, to_span(st.read_buffer.prepare(res.hint())));

                // Check for read errors
                if (io_ec)
                    return io_ec;

                // Commit the data and try again
                st.read_buffer.commit(bytes_read);
            }
        }
    }

    BOOST_ASSERT(false);
    return error_code();
}

static error_code check_request(const nativepg::request& req)
{
    // Empty requests are not allowed
    if (req.messages().empty())
        return error_code(client_errc::empty_request);

    // To enable appropriate error recovery, requests need to either
    //   1. End with a sync
    //   2. Be a sequence of query messages, with nothing in front of it
    //   3. End with a sequence of query messages, with a sync in front of it
    // TODO: do we need to support requests ending with flush? is there any use case for this?
    bool query_seen = false;
    for (auto it = req.messages().rbegin(); it != req.messages().rend(); ++it)
    {
        switch (*it)
        {
            case nativepg::request_message_type::query: query_seen = true; continue;
            case nativepg::request_message_type::sync: return error_code();
            default:
                return query_seen ? client_errc::request_mixes_simple_advanced_protocols
                                  : client_errc::request_ends_without_sync;
        }
    }

    // There was nothing but queries
    return error_code();
}

exec_fsm::result exec_fsm::resume(
    connection_state& st,
    boost::system::error_code ec,
    std::size_t bytes_transferred
)
{
    if (state_ == state_t::initial)
    {
        // Check that the request is correctly formed
        const request& req = read_fsm_.get_request();
        auto ec = check_request(req);
        if (ec)
            return ec;

        // Perform the response setup
        auto res = read_fsm_.get_handler().setup(req, 0u);
        if (res.ec)
            return res.ec;
        if (res.offset != req.messages().size())
            return result(client_errc::incompatible_response_length);

        state_ = state_t::writing;

        // Write the request
        return result::write(read_fsm_.get_request().payload());
    }

    if (ec)
        return ec;

    // Don't pass the bytes read to the first FSM invocation
    if (state_ == state_t::writing)
    {
        bytes_transferred = 0u;
        state_ = state_t::reading;
    }
    auto act = read_fsm_.resume(st, ec, bytes_transferred);
    switch (act.type())
    {
        case read_response_fsm::result_type::read: return result::read(act.read_buffer());
        case read_response_fsm::result_type::done: return act.error();
        default: BOOST_ASSERT(false); return error_code();
    }
}

static connect_fsm::result to_connect_result(const startup_fsm::result& r)
{
    switch (r.type())
    {
        case startup_fsm::result_type::read: return connect_fsm::result::read(r.read_buffer());
        case startup_fsm::result_type::write: return connect_fsm::result::write(r.write_data());
        default: BOOST_ASSERT(false); return error_code();
    }
}

connect_fsm::result connect_fsm::resume(
    connection_state& st,
    boost::system::error_code ec,
    std::size_t bytes_transferred
)
{
    startup_fsm::result res{error_code()};

    switch (resume_point_)
    {
        NATIVEPG_CORO_INITIAL

        // Physical connect
        NATIVEPG_YIELD(resume_point_, 1, result::connect())

        // If this failed, try to close. Ignore any errors
        if (ec)
        {
            stored_ec_ = ec;
            NATIVEPG_YIELD(resume_point_, 2, result::close())
            return stored_ec_;
        }

        // Call the startup algorithm
        while (true)
        {
            res = startup_.resume(st, st.shared_diag, ec, bytes_transferred);
            if (res.type() == startup_fsm::result_type::done)
                break;
            else
                NATIVEPG_YIELD(resume_point_, 3, to_connect_result(res))
        }

        // Attempt a close if the startup process failed
        if (res.error())
        {
            stored_ec_ = res.error();
            NATIVEPG_YIELD(resume_point_, 4, result::close())
            return stored_ec_;
        }

        // Success
        return error_code();
    }

    BOOST_ASSERT(false);
    return error_code();
}
