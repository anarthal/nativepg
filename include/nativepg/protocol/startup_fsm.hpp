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
            std::span<const unsigned char> write_data_;
        };

        explicit result(result_type t) noexcept : type_(t) {}

    public:
        result(boost::system::error_code ec) noexcept : type_(result_type::done), ec_(ec) {}
        result(std::span<const unsigned char> write_data) noexcept
            : type_(result_type::write), write_data_(write_data)
        {
        }

        // TODO: this is asymmetric
        static result read() { return result{result_type::read}; }

        result_type type() const { return type_; }

        boost::system::error_code error() const
        {
            BOOST_ASSERT(type_ == result_type::done);
            return ec_;
        }
        std::span<const unsigned char> write_data() const
        {
            BOOST_ASSERT(type_ == result_type::write);
            return write_data_;
        }
    };

    explicit startup_fsm(const startup_params& params) noexcept : params_(&params) {}

    result resume(
        connection_state& st,
        boost::system::error_code io_error,
        const any_backend_message& msg = {}
    );

private:
    int resume_point_{0};
    const startup_params* params_;
};

}  // namespace nativepg::protocol

#endif
