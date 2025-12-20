//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/asio/buffer.hpp>
#include <boost/system/system_error.hpp>
#include <boost/variant2/variant.hpp>

#include <cstddef>
#include <span>
#include <type_traits>

#include "coroutine.hpp"
#include "nativepg/client_errc.hpp"
#include "nativepg/protocol/async.hpp"
#include "nativepg/protocol/bind.hpp"
#include "nativepg/protocol/close.hpp"
#include "nativepg/protocol/connection_state.hpp"
#include "nativepg/protocol/header.hpp"
#include "nativepg/protocol/messages.hpp"
#include "nativepg/protocol/notice_error.hpp"
#include "nativepg/protocol/read_message_fsm.hpp"
#include "nativepg/protocol/read_response_fsm.hpp"
#include "nativepg/protocol/ready_for_query.hpp"
#include "nativepg/protocol/startup.hpp"
#include "nativepg/protocol/startup_fsm.hpp"
#include "nativepg/request.hpp"

using namespace nativepg::protocol;
using boost::system::error_code;
using detail::read_response_fsm_impl;
using detail::startup_fsm_impl;
using nativepg::client_errc;
using nativepg::detail::request_access;
using nativepg::detail::request_msg_type;

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

namespace {

struct startup_visitor
{
    error_code operator()(const error_response&) const
    {
        // TODO: string diagnostics
        return client_errc::auth_failed;
    }

    error_code operator()(const authentication_ok&) const { return error_code(); }

    error_code operator()(const authentication_kerberos_v5&) const
    {
        return client_errc::auth_kerberos_v5_unsupported;
    }

    error_code operator()(const authentication_cleartext_password&) const
    {
        return client_errc::auth_cleartext_password_unsupported;
    }

    error_code operator()(const authentication_md5_password&) const
    {
        return client_errc::auth_md5_password_unsupported;
    }

    error_code operator()(const authentication_gss&) const { return client_errc::auth_gss_unsupported; }

    error_code operator()(const authentication_sspi&) const { return client_errc::auth_sspi_unsupported; }

    error_code operator()(const authentication_sasl&) const { return client_errc::auth_sasl_unsupported; }

    template <class T>
    error_code operator()(const T&) const
    {
        return client_errc::unexpected_message;
    }
};

}  // namespace

startup_fsm_impl::result startup_fsm_impl::resume(connection_state& st, const any_backend_message& msg)
{
    switch (resume_point_)
    {
        NATIVEPG_CORO_INITIAL

        // Compose the startup message
        st.write_buffer.clear();
        if (auto ec = serialize(
                startup_message{
                    .user = params_->username,
                    .database = params_->database,
                    .params = {},
                },
                st.write_buffer
            ))
        {
            return ec;
        }

        // Write it
        NATIVEPG_YIELD(resume_point_, 1, result_type::write)

        // Read the server's response
        NATIVEPG_YIELD(resume_point_, 2, result_type::read)

        // Act upon the server's message
        // TODO: this will have to change once we implement SASL
        if (auto ec = boost::variant2::visit(startup_visitor{}, msg))
        {
            return ec;
        }

        // Backend has approved our login request. Now wait until we receive ReadyForQuery
        while (true)
        {
            // Read a message
            NATIVEPG_YIELD(resume_point_, 3, result_type::read)

            // Act upon it
            if (const auto* key = boost::variant2::get_if<backend_key_data>(&msg))
            {
                st.backend_process_id = key->process_id;
                st.backend_secret_key = key->secret_key;
            }
            else if (boost::variant2::holds_alternative<parameter_status>(msg))
            {
                // TODO: record these somehow
            }
            else if (boost::variant2::holds_alternative<error_response>(msg))
            {
                return error_code(client_errc::auth_failed);
            }
            else if (boost::variant2::holds_alternative<notice_response>(msg))
            {
                // TODO: record these somehow
            }
            else if (boost::variant2::holds_alternative<ready_for_query>(msg))
            {
                return error_code();
            }
            else
            {
                return error_code(client_errc::unexpected_message);
            }
        }
    }

    // We should never reach here
    BOOST_ASSERT(false);
    return error_code();
}

startup_fsm::result startup_fsm::resume(
    connection_state& st,
    boost::system::error_code io_error,
    std::size_t bytes_read
)
{
    // TODO: this implementation is improvable, changes in reading messages required
    any_backend_message msg;
    startup_fsm_impl::result startup_res{error_code()};
    read_message_stream_fsm::result read_msg_res{error_code()};

    switch (resume_point_)
    {
        NATIVEPG_CORO_INITIAL

        while (true)
        {
            // Call the FSM
            startup_res = impl_.resume(st, msg);
            if (startup_res.type == startup_fsm_impl::result_type::done)
            {
                // We're finished
                return startup_res.ec;
            }
            else if (startup_res.type == startup_fsm_impl::result_type::write)
            {
                // Just write the message
                NATIVEPG_YIELD(resume_point_, 1, result::write(st.write_buffer))

                // Check for errors
                if (io_error)
                    return io_error;
            }
            else
            {
                BOOST_ASSERT(startup_res.type == startup_fsm_impl::result_type::read);

                // Read a message
                while (true)
                {
                    read_msg_res = st.read_msg_stream_fsm.resume(st, io_error, bytes_read);
                    if (read_msg_res.type() == read_message_stream_fsm::result_type::read)
                    {
                        NATIVEPG_YIELD(resume_point_, 2, result::read(read_msg_res.read_buffer()));
                    }
                    else if (read_msg_res.type() == read_message_stream_fsm::result_type::message)
                    {
                        msg = read_msg_res.message();
                        break;
                    }
                    else
                    {
                        BOOST_ASSERT(read_msg_res.type() == read_message_stream_fsm::result_type::error);
                        return read_msg_res.error();
                    }
                }
            }
        }
    }

    BOOST_ASSERT(false);
    return error_code();
}

namespace {

std::size_t count_syncs(std::span<const request_msg_type> msgs)
{
    std::size_t res = 0;
    for (const auto msg : msgs)
        res += static_cast<std::size_t>(msg == request_msg_type::query || msg == request_msg_type::sync);
    return res;
}

}  // namespace

read_response_fsm_impl::result read_response_fsm_impl::resume(const any_backend_message& msg)
{
    // Initial checks
    if (initial_)
    {
        const auto msg_types = request_access::messages(*req_);

        // Empty requests are not allowed
        if (msg_types.empty())
            return error_code(client_errc::empty_request);

        // Currently, all pipelines must end with a sync or a query
        const auto last_type = msg_types.back();
        if (last_type != request_msg_type::sync && last_type != request_msg_type::query)
            return error_code(client_errc::request_ends_without_sync);

        // Count how many syncs are we expecting
        remaining_syncs_ = count_syncs(msg_types);

        initial_ = false;
    }

    // Discard asynchronous messages that might be received at any time
    // TODO: do something useful with these
    if (boost::variant2::holds_alternative<notice_response>(msg) ||
        boost::variant2::holds_alternative<notification_response>(msg) ||
        boost::variant2::holds_alternative<parameter_status>(msg))
    {
        return result_type::read;
    }

    // If this is a ReadyForQuery, check if we're done
    if (boost::variant2::holds_alternative<ready_for_query>(msg))
    {
        if (--remaining_syncs_ == 0u)
        {
            return handler_finished_ ? error_code() : client_errc::needs_more;
        }
        else
        {
            return result_type::read;
        }
    }

    // This is either a message for the handler, or an unknown message
    return boost::variant2::visit(
        [this](const auto& typed_msg) -> result {
            using T = decltype(typed_msg);
            if constexpr (std::is_same_v<T, bind_complete> || std::is_same_v<T, close_complete> ||
                          std::is_same_v<T, command_complete> || std::is_same_v<T, data_row> ||
                          std::is_same_v<T, parameter_description> || std::is_same_v<T, row_description> ||
                          std::is_same_v<T, no_data> || std::is_same_v<T, empty_query_response> ||
                          std::is_same_v<T, portal_suspended> || std::is_same_v<T, error_response> ||
                          std::is_same_v<T, parse_complete>)
            {
                auto res = handler_(typed_msg);
                if (res)
                {
                    return res == client_errc::needs_more ? result(result_type::read) : res;
                }
                else
                {
                    handler_finished_ = true;
                    return result_type::read;
                }
            }
            else
            {
                return error_code(client_errc::unexpected_message);
            }
        },
        msg
    );
}
