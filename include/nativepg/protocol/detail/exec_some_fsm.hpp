//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_PROTOCOL_DETAIL_EXEC_FSM_HPP
#define NATIVEPG_PROTOCOL_DETAIL_EXEC_FSM_HPP

#include <boost/assert.hpp>
#include <boost/capy/buffers.hpp>
#include <boost/capy/buffers/make_buffer.hpp>
#include <boost/system/error_code.hpp>

#include <cstddef>
#include <system_error>
#include <vector>

#include "coroutine.hpp"
#include "nativepg/protocol/connection_state.hpp"
#include "nativepg/protocol/parse_message.hpp"
#include "nativepg/protocol/read_response_fsm.hpp"
#include "nativepg/protocol/startup_fsm.hpp"
#include "nativepg/request.hpp"
#include "nativepg/response_handler.hpp"
#include "nativepg_internal/check_request.hpp"

namespace nativepg::protocol::detail {

// TODO: test
// TODO: separate compilation
class exec_some_fsm
{
public:
    enum class result_type
    {
        write,
        read,
        copy_out,
        copy_data,
        copy_data_with_eof,
        done,
    };

    class result
    {
    public:
        result(std::error_code ec = {}) noexcept : type_(result_type::done), data_(ec) {}
        result(result_type t) noexcept : type_(t) {}
        result(protocol::copy_out_response value) noexcept : type_(result_type::copy_out), data_(value) {}

        result_type type() const { return type_; }
        protocol::copy_out_response get_copy_out() const
        {
            BOOST_ASSERT(type_ == result_type::copy_out);
            return data_.copy_out;
        }

    private:
        result_type type_{result_type::done};

        union data_t
        {
            std::error_code ec;
            protocol::copy_out_response copy_out;

            data_t(std::error_code ec = {}) noexcept : ec(ec) {}
            data_t(protocol::copy_out_response value) noexcept : copy_out(value) {}
        } data_;
    };

    exec_some_fsm(const request* req, response_handler_ref handler) noexcept : fsm_(req, handler) {}

    result resume(connection_state& st, std::vector<boost::capy::const_buffer>& copy_buffs)
    {
        parse_message_result res;

        switch (resume_point_)
        {
            NATIVEPG_CORO_INITIAL

            // Initial checks
            if (auto req_ec = setup_request(fsm_.get_request(), fsm_.get_handler()))
                return {req_ec};

            copy_buffs.clear();

            // Write the request to the server
            NATIVEPG_YIELD(resume_point_, 1, result_type::write)

            // Read the response
            while (true)
            {
                // Get a message batch
                st.read_buffer.consume(consumed_);
                consumed_ = 0u;
                NATIVEPG_YIELD(resume_point_, 2, result_type::read)

                // Process the batch
                while (true)
                {
                    // Try to get a message
                    res = parse_message(st.read_buffer.committed_area());
                    if (res.ec == client_errc::needs_more)
                    {
                        // We need more data. If we have received any copy data, yield
                        if (!copy_buffs.empty())
                        {
                            NATIVEPG_YIELD(resume_point_, 3, result_type::copy_data)
                            copy_buffs.clear();
                        }

                        // Keep reading
                        continue;
                    }
                    else if (res.ec)
                        return {res.ec};  // Unrecoverable error

                    // We have a message, process it
                    consumed_ += res.size;

                    // Check if the message is legal in our state,
                    // and if it ends the sequence we're looking for.
                    // Copy messages are never the last one, so this is safe
                    if (auto read_res = fsm_.resume(res.message);
                        read_res.type == protocol::read_response_fsm::result_type::done)
                    {
                        st.read_buffer.consume(consumed_);
                        return {read_res.ec};
                    }

                    // React to copy messages
                    if (res.message.type() == any_backend_message::kind::copy_out_response)
                    {
                        // We're entering COPY OUT, notify the user
                        NATIVEPG_YIELD(resume_point_, 4, res.message.get_copy_out_response())
                    }
                    else if (res.message.type() == any_backend_message::kind::copy_data)
                    {
                        // Store them, we'll return the entire batch when ready
                        copy_buffs.push_back(boost::capy::make_buffer(res.message.get_copy_data().data));
                    }
                    else if (res.message.type() == any_backend_message::kind::copy_done)
                    {
                        // The copy batch has finished, return the remaining messages and the eof
                        NATIVEPG_YIELD(resume_point_, 5, result_type::copy_data_with_eof)
                        copy_buffs.clear();
                    }
                }
            }
        }

        BOOST_ASSERT(false);
        return {};
    }

private:
    int resume_point_{0};
    std::size_t consumed_{};
    read_response_fsm fsm_;
};

}  // namespace nativepg::protocol::detail

#endif
