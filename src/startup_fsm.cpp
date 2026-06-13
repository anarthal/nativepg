//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/system/error_code.hpp>
#include <boost/system/result.hpp>

#include <algorithm>
#include <string_view>

#include "coroutine.hpp"
#include "nativepg/client_errc.hpp"
#include "nativepg/connect_params.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/protocol/any_backend_message.hpp"
#include "nativepg/protocol/connection_state.hpp"
#include "nativepg/protocol/messages_view.hpp"
#include "nativepg/protocol/startup.hpp"
#include "nativepg/protocol/startup_fsm.hpp"

using namespace nativepg::protocol;
using boost::system::error_code;
using detail::startup_fsm_impl;
using nativepg::client_errc;
using kind = any_backend_message::kind;

namespace {

error_code check_unknown_auth_methods(const any_backend_message& msg)
{
    switch (msg.type())
    {
        case kind::authentication_kerberos_v5: return client_errc::auth_kerberos_v5_unsupported;
        case kind::authentication_cleartext_password: return client_errc::auth_cleartext_password_unsupported;
        case kind::authentication_md5_password: return client_errc::auth_md5_password_unsupported;
        case kind::authentication_gss: return client_errc::auth_gss_unsupported;
        case kind::authentication_sspi: return client_errc::auth_sspi_unsupported;
        default: return error_code();
    }
}

error_code handle_auth_response(const any_backend_message& msg, nativepg::diagnostics& diag)
{
    switch (msg.type())
    {
        case kind::error_response: diag.assign(msg.get_error_response()); return client_errc::auth_failed;
        case kind::authentication_ok: return error_code();
        default: return client_errc::unexpected_message;
    }
}

startup_message make_startup_message(const nativepg::connect_params& params)
{
    return {
        .user = params.username,
        .database = params.database.empty() ? std::optional<std::string_view>()
                                            : std::string_view(params.database),
        .params = {},
    };
}

// Mechanism name
constexpr std::string_view scram_sha256_name = "SCRAM-SHA-256";

// Does the server support SCRAM-SHA-256?
bool supports_scram_sha256(const authentication_sasl& msg)
{
    return std::find(msg.mechanisms.begin(), msg.mechanisms.end(), scram_sha256_name) != msg.mechanisms.end();
}

}  // namespace

startup_fsm_impl::result startup_fsm_impl::resume(
    connection_state& st,
    diagnostics& diag,
    const any_backend_message& msg
)
{
    switch (resume_point_)
    {
        NATIVEPG_CORO_INITIAL

        // Compose the startup message
        st.write_buffer.clear();
        if (auto ec = serialize(make_startup_message(*params_), st.write_buffer))
        {
            return ec;
        }

        // Write it
        NATIVEPG_YIELD(resume_point_, 1, result_type::write)

        // Read the server's response
        NATIVEPG_YIELD(resume_point_, 2, result_type::read)

        // Handle authentication requests
        if (msg.type() == any_backend_message::kind::authentication_sasl)
        {
            // SASL authentication was requested. Check the mechanisms
            if (!supports_scram_sha256(msg.get_authentication_sasl()))
                return error_code(client_errc::scram_mechanisms_unsupported);

            // Delegate to the SCRAM FSM. This generates the message to send to the server
            if (auto ec = scram_fsm_.on_init(st.write_buffer))
                return ec;

            // Write the message
            NATIVEPG_YIELD(resume_point_, 3, result_type::write)

            // Read the server's response
            NATIVEPG_YIELD(resume_point_, 4, result_type::read)

            switch (msg.type())
            {
                // TODO: can notices be received here?
                case any_backend_message::kind::error_response:
                    diag.assign(msg.get_error_response());
                    return error_code(client_errc::auth_failed);
                case any_backend_message::kind::authentication_sasl_continue: break;
                default: return error_code(client_errc::unexpected_message);
            }

            // Process it. This generates the message to send to the server
            if (auto ec = scram_fsm_.on_server_first(
                    msg.get_authentication_sasl_continue().data,
                    params_->password,
                    st.write_buffer
                ))
            {
                return ec;
            }

            // Write the message
            NATIVEPG_YIELD(resume_point_, 5, result_type::write)

            // Read the server's response
            NATIVEPG_YIELD(resume_point_, 6, result_type::read)

            switch (msg.type())
            {
                // TODO: can notices be received here?
                case any_backend_message::kind::error_response:
                    diag.assign(msg.get_error_response());
                    return error_code(client_errc::auth_failed);
                case any_backend_message::kind::authentication_sasl_final: break;
                default: return error_code(client_errc::unexpected_message);
            }

            // Check the server's final message
            if (auto ec = scram_fsm_.on_server_final(msg.get_authentication_sasl_final().data))
                return ec;

            // SCRAM authentication is done. We should wait until an OK is received
            NATIVEPG_YIELD(resume_point_, 7, result_type::read)
        }
        else if (auto ec = check_unknown_auth_methods(msg))
        {
            // We don't know this auth method
            return ec;
        }

        // After any authentication has been performed, the server should send us a response
        if (auto ec = handle_auth_response(msg, diag))
            return ec;

        // Backend has approved our login request. Now wait until we receive ReadyForQuery
        while (true)
        {
            // Read a message
            NATIVEPG_YIELD(resume_point_, 8, result_type::read)

            // Act upon it
            switch (msg.type())
            {
                case kind::backend_key_data:
                {
                    const auto& key = msg.get_backend_key_data();
                    st.backend_process_id = key.process_id;
                    st.backend_secret_key = key.secret_key;
                    break;
                }
                case kind::parameter_status:
                    // TODO: record these somehow
                    break;
                case kind::error_response:
                    diag.assign(msg.get_error_response());
                    return error_code(client_errc::auth_failed);
                case kind::notice_response:
                    // TODO: record these somehow
                    break;
                case kind::ready_for_query: return error_code();
                default: return error_code(client_errc::unexpected_message);
            }
        }
    }

    // We should never reach here
    BOOST_ASSERT(false);
    return error_code();
}

startup_fsm::result startup_fsm::resume(
    connection_state& st,
    diagnostics& diag,
    boost::system::error_code io_error,
    std::size_t bytes_read
)
{
    // TODO: this implementation is improvable, changes in reading messages required
    startup_fsm_impl::result startup_res{error_code()};
    messages_view::result read_msg_res;

    switch (resume_point_)
    {
        NATIVEPG_CORO_INITIAL

        startup_res = impl_.resume(st, diag, {});

        while (true)
        {
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

                startup_res = impl_.resume(st, diag, {});
            }
            else
            {
                BOOST_ASSERT(startup_res.type == startup_fsm_impl::result_type::read);

                // Read a message
                while (true)
                {
                    // Try to get a cached message
                    read_msg_res = messages_view(st.read_buffer.committed_area()).next();
                    if (!read_msg_res.ec)
                    {
                        // We have a message
                        startup_res = impl_.resume(st, diag, read_msg_res.message);
                        st.read_buffer.consume(read_msg_res.size);
                        break;
                    }
                    else if (read_msg_res.ec == client_errc::needs_more)
                    {
                        // Make space in the buffer, if required
                        st.read_buffer.prepare(read_msg_res.size);

                        // Read some data
                        NATIVEPG_YIELD(resume_point_, 2, result::read(st.read_buffer.prepared_area()))

                        // Check for errors
                        if (io_error)
                            return io_error;

                        // Commit the data we were handed in
                        st.read_buffer.commit(bytes_read);
                    }
                    else
                    {
                        // An error occurred
                        return read_msg_res.ec;
                    }
                }
            }
        }
    }

    BOOST_ASSERT(false);
    return error_code();
}
