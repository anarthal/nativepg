#pragma once

#include <boost/core/span.hpp>
#include <boost/system/detail/error_code.hpp>
#include <boost/system/result.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "nativepg/client_errc.hpp"
#include "nativepg/wire/parse_context.hpp"

namespace nativepg {
namespace detail {

enum class backend_message_type : char
{
    invalid = 0,
    authentication_request = 'R',
    backend_key_data = 'K',
    bind_complete = '2',
    close_complete = '3',
    command_complete = 'C',
};

struct authentication_request_message
{
    enum class type_t
    {
        ok,
        cleartext,
        md5,
    };

    union data_t
    {
        std::nullptr_t nothing;
        std::array<unsigned char, 4> md5_salt;

        data_t() noexcept : nothing(nullptr) {}
        data_t(std::array<unsigned char, 4> v) noexcept : md5_salt(v) {}
    };

    type_t type;
    data_t data;

    void parse(parse_context& ctx)
    {
        auto type_code = ctx.get_integral<std::int32_t>();
        switch (type_code)
        {
        case 0:
            type = type_t::ok;
            data = data_t{};
            break;
        case 3:
            type = type_t::cleartext;
            data = data_t{};
            break;
        case 5:
            type = type_t::md5;
            data = data_t(ctx.get_byte_array<4>());
            break;
        default: ctx.add_error(client_errc::protocol_value_error); break;
        }
    }
};

struct backend_key_data_message
{
    // The process ID of this backend.
    std::int32_t process_id;

    // The secret key of this backend.
    std::int32_t secret_key;

    void parse(parse_context& ctx)
    {
        process_id = ctx.get_integral<std::int32_t>();
        secret_key = ctx.get_integral<std::int32_t>();
    }
};

struct bind_complete_message
{
    void parse(parse_context&) {}
};

struct close_complete_message
{
    void parse(parse_context&) {}
};

struct command_complete_message
{
    // The command tag. This is usually a single word that identifies which SQL command was completed.
    std::string_view tag;

    void parse(parse_context& ctx) { tag = ctx.get_string(); }
};

struct backend_message
{
    union data_t
    {
        boost::system::error_code error;
        authentication_request_message authentication_request;
        backend_key_data_message backend_key_data;
        bind_complete_message bind_complete;
        close_complete_message close_complete;
        command_complete_message command_complete;

        data_t(boost::system::error_code v) noexcept : error(v) {}
        data_t(authentication_request_message v) noexcept : authentication_request(v) {}
        data_t(backend_key_data_message v) noexcept : backend_key_data(v) {}
        data_t(bind_complete_message v) noexcept : bind_complete(v) {}
        data_t(close_complete_message v) noexcept : close_complete(v) {}
        data_t(command_complete_message v) noexcept : command_complete(v) {}
    };

    backend_message_type type;
    data_t data;
};

// When the type is known
template <class Message>
inline boost::system::result<Message> parse_known_message(boost::span<const unsigned char> bytes)
{
    parse_context ctx(bytes);
    Message res;
    res.parse(ctx);
    ctx.check_extra_bytes();
    boost::system::error_code err = ctx.error();
    if (err)
        return err;
    return res;
}

template <class Message>
backend_message parse_message_impl(backend_message_type type, boost::span<const unsigned char> data)
{
    auto res = parse_known_message<Message>(data);
    if (res.has_error())
        return {backend_message_type::invalid, res.error()};
    return {type, *res};
}

inline backend_message parse_message(unsigned char type, boost::span<const unsigned char> data)
{
    auto t = static_cast<backend_message_type>(type);
    switch (t)
    {
    case backend_message_type::authentication_request:
        return parse_message_impl<authentication_request_message>(t, data);
    case backend_message_type::backend_key_data: return parse_message_impl<backend_key_data_message>(t, data);
    case backend_message_type::bind_complete: return parse_message_impl<bind_complete_message>(t, data);
    case backend_message_type::close_complete: return parse_message_impl<close_complete_message>(t, data);
    case backend_message_type::command_complete: return parse_message_impl<command_complete_message>(t, data);
    default:
        return {backend_message_type::invalid, boost::system::error_code(client_errc::protocol_value_error)};
    }
}

}  // namespace detail
}  // namespace nativepg