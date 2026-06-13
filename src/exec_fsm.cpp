//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/system/error_code.hpp>

#include <cstddef>

#include "coroutine.hpp"
#include "nativepg/client_errc.hpp"
#include "nativepg/protocol/connection_state.hpp"
#include "nativepg/protocol/detail/exec_fsm.hpp"
#include "nativepg/protocol/detail/read_buffer.hpp"
#include "nativepg/protocol/parse_message.hpp"
#include "nativepg/protocol/read_response_fsm.hpp"
#include "nativepg/protocol/startup_fsm.hpp"
#include "nativepg/request.hpp"
#include "nativepg_internal/check_request.hpp"

using namespace nativepg::protocol;
using boost::system::error_code;
using detail::exec_fsm;
using nativepg::client_errc;

exec_fsm::result exec_fsm::resume(
    connection_state& st,
    boost::system::error_code ec,
    std::size_t bytes_transferred
)
{
    read_response_fsm::result res{{}};
    parse_message_result msg_res;

    switch (resume_point_)
    {
        NATIVEPG_CORO_INITIAL

        // Initial checkings
        if (auto ec_req = setup_request(read_fsm_.get_request(), read_fsm_.get_handler()))
            return ec_req;

        // Write the request
        NATIVEPG_YIELD(resume_point_, 1, result::write(read_fsm_.get_request().payload()))
        if (ec)
            return ec;

        // Read the response
        while (true)
        {
            // Try to get a cached message
            msg_res = parse_message(st.read_buffer.committed_area());
            if (!msg_res.ec)
            {
                // We have a message
                res = read_fsm_.resume(msg_res.message);
                st.read_buffer.consume(msg_res.size);
                if (res.type == read_response_fsm::result_type::done)
                    return res.ec;
            }
            else if (msg_res.ec == client_errc::needs_more)
            {
                // Make space in the buffer, if required
                st.read_buffer.prepare(msg_res.size);

                // Read some data
                NATIVEPG_YIELD(resume_point_, 2, result::read(st.read_buffer.prepared_area()))

                // Check for errors
                if (ec)
                    return ec;

                // Commit the data we were handed in
                st.read_buffer.commit(bytes_transferred);
            }
            else
            {
                // An error occurred
                return msg_res.ec;
            }
        }
    }

    // We should never reach here
    BOOST_ASSERT(false);
    return boost::system::error_code();
}
