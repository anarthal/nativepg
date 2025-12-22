//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_PROTOCOL_DETAIL_CONNECT_FSM_HPP
#define NATIVEPG_PROTOCOL_DETAIL_CONNECT_FSM_HPP

#include <boost/assert.hpp>
#include <boost/system/error_code.hpp>

#include <cstddef>

#include "nativepg/connect_params.hpp"
#include "nativepg/protocol/startup_fsm.hpp"

namespace nativepg::protocol::detail {

// TODO: test
class connect_fsm
{
public:
    enum class result_type
    {
        done,
        read,
        write,
        connect,
        close,
    };

    class result
    {
        result_type type_;
        union
        {
            boost::system::error_code ec_;
            std::span<unsigned char> data_;
        };

        result(result_type t, std::span<unsigned char> data = {}) noexcept : type_(t), data_(data) {}

    public:
        result(boost::system::error_code ec) noexcept : type_(result_type::done), ec_(ec) {}

        static result connect() { return {result_type::connect}; }
        static result close() { return {result_type::close}; }
        static result read(std::span<unsigned char> buff) { return {result_type::read, buff}; }
        static result write(std::span<const unsigned char> buff)
        {
            return {
                result_type::write,
                {const_cast<unsigned char*>(buff.data()), buff.size()}
            };
        }

        result_type type() const { return type_; }

        boost::system::error_code error() const
        {
            BOOST_ASSERT(type_ == result_type::done);
            return ec_;
        }
        std::span<const unsigned char> write_data() const
        {
            BOOST_ASSERT(type_ == result_type::write);
            return data_;
        }
        std::span<unsigned char> read_buffer() const
        {
            BOOST_ASSERT(type_ == result_type::read);
            return data_;
        }
    };

    connect_fsm(const connect_params& params) noexcept : startup_(params) {}

    result resume(connection_state& st, boost::system::error_code ec, std::size_t bytes_transferred);

private:
    int resume_point_{0};
    boost::system::error_code stored_ec_;
    startup_fsm startup_;
};

}  // namespace nativepg::protocol::detail

#endif
