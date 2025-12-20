//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_PROTOCOL_STARTUP_FSM_HPP
#define NATIVEPG_PROTOCOL_STARTUP_FSM_HPP

#include <boost/assert.hpp>
#include <boost/system/error_code.hpp>

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

#include "nativepg/protocol/connection_state.hpp"
#include "nativepg/protocol/messages.hpp"

namespace nativepg::protocol {

struct startup_params
{
    std::string_view username;
    std::string_view password;
    std::optional<std::string_view> database;
    // TODO: support arbitrary startup params?
};

namespace detail {

class startup_fsm_impl
{
public:
    enum class result_type
    {
        done,
        read,
        write,
    };

    struct result
    {
        result_type type;
        boost::system::error_code ec;

        result(boost::system::error_code ec) noexcept : type(result_type::done), ec(ec) {}
        result(result_type t) noexcept : type(t) {}
    };

    explicit startup_fsm_impl(const startup_params& params) noexcept : params_(&params) {}

    result resume(connection_state& st, const any_backend_message& msg = {});

private:
    int resume_point_{0};
    const startup_params* params_;
};

}  // namespace detail

class startup_fsm
{
public:
    enum class result_type
    {
        done,
        read,
        write,
    };

    class result
    {
        result_type type_;
        union
        {
            boost::system::error_code ec_;
            std::span<unsigned char> data_;
        };

        result(result_type t, std::span<unsigned char> data) noexcept : type_(t), data_(data) {}

    public:
        result(boost::system::error_code ec) noexcept : type_(result_type::done), ec_(ec) {}

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

    explicit startup_fsm(const startup_params& params) noexcept : impl_(params) {}

    result resume(connection_state& st, boost::system::error_code io_error, std::size_t bytes_read);

private:
    int resume_point_{0};
    detail::startup_fsm_impl impl_;
};

}  // namespace nativepg::protocol

#endif
