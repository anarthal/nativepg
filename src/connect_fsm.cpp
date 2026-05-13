//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/system/error_code.hpp>

#include <cstddef>

#include "coroutine.hpp"
#include "nativepg/protocol/connection_state.hpp"
#include "nativepg/protocol/detail/connect_fsm.hpp"
#include "nativepg/protocol/startup_fsm.hpp"

using namespace nativepg::protocol;
using boost::system::error_code;
using detail::connect_fsm;
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
