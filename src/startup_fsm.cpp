//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/system/error_code.hpp>

#include "coroutine.hpp"
#include "nativepg/client_errc.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/protocol/connection_state.hpp"
#include "nativepg/protocol/startup.hpp"
#include "nativepg/protocol/startup_fsm.hpp"

using namespace nativepg::protocol;
using boost::system::error_code;
using detail::startup_fsm_impl;
using nativepg::client_errc;
using kind = any_backend_message::kind;

namespace {

error_code handle_startup_response(const any_backend_message& msg, nativepg::diagnostics& diag)
{
    switch (msg.type())
    {
        case kind::error_response: diag.assign(msg.get_error_response()); return client_errc::auth_failed;
        case kind::authentication_ok: return error_code();
        case kind::authentication_kerberos_v5: return client_errc::auth_kerberos_v5_unsupported;
        case kind::authentication_cleartext_password: return client_errc::auth_cleartext_password_unsupported;
        case kind::authentication_md5_password: return client_errc::auth_md5_password_unsupported;
        case kind::authentication_gss: return client_errc::auth_gss_unsupported;
        case kind::authentication_sspi: return client_errc::auth_sspi_unsupported;
        case kind::authentication_sasl: return client_errc::auth_sasl_unsupported;
        default: return client_errc::unexpected_message;
    }
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
        if (auto ec = serialize(
                startup_message{
                    .user = params_->username,
                    .database = params_->database.empty() ? std::optional<std::string_view>()
                                                          : std::string_view(params_->database),
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
        if (auto ec = handle_startup_response(msg, diag))
        {
            return ec;
        }

        // Backend has approved our login request. Now wait until we receive ReadyForQuery
        while (true)
        {
            // Read a message
            NATIVEPG_YIELD(resume_point_, 3, result_type::read)

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
    any_backend_message msg;
    startup_fsm_impl::result startup_res{error_code()};
    read_message_stream_fsm::result read_msg_res{error_code()};

    switch (resume_point_)
    {
        NATIVEPG_CORO_INITIAL

        while (true)
        {
            // Call the FSM
            startup_res = impl_.resume(st, diag, msg);
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
