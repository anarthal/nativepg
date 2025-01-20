#pragma once

#include <boost/core/span.hpp>
#include <boost/endian/conversion.hpp>
#include <boost/system/detail/error_code.hpp>
#include <boost/system/result.hpp>
#include <boost/variant2/variant.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "nativepg/client_errc.hpp"

namespace nativepg {
namespace protocol {

namespace detail {
boost::system::error_code check_empty(boost::span<const unsigned char> data);
}

// Message header operations
struct message_header
{
    std::uint8_t type;  // The message type
    std::int32_t size;  // Should be >= 0
};

inline void serialize_header(message_header header, boost::span<unsigned char, 5> dest)
{
    unsigned char* ptr = dest.data();
    *ptr++ = header.type;
    boost::endian::store_big_s32(ptr, header.size);
}

inline message_header parse_header(boost::span<const unsigned char, 5> from)
{
    return {from[0], boost::endian::load_big_s32(from.data() + 1u)};
}

//
// Messages that we may receive from the backend
//

// Authentication messages require parsing the following 4 bytes, too
struct authentication_ok
{
};
inline boost::system::error_code parse(boost::span<const unsigned char> data, authentication_ok&)
{
    return detail::check_empty(data);
}

struct authentication_cleartext_password
{
};
inline boost::system::error_code parse(boost::span<const unsigned char> data, authentication_cleartext_password&)
{
    return detail::check_empty(data);
}

struct authentication_md5_password
{
    // The salt to use when encrypting the password.
    std::array<unsigned char, 4> salt;
};
boost::system::error_code parse(boost::span<const unsigned char> data, authentication_md5_password&);

// TODO: rest of authentication messages
// TODO: do we want to expose a fn to parse any authentication message? (they share message code, type is
// determined with the following bytes)

struct backend_key_data
{
    // The process ID of this backend.
    std::int32_t process_id;

    // The secret key of this backend.
    std::int32_t secret_key;
};
boost::system::error_code parse(boost::span<const unsigned char> data, backend_key_data& to);

// TODO: this should be at the end
// TODO: this is not the most efficient
using any_backend_message = boost::variant2::variant<
    authentication_ok,
    authentication_cleartext_password,
    authentication_md5_password,
    backend_key_data>;
boost::system::result<any_backend_message> parse(
    std::uint8_t message_type,
    boost::span<const unsigned char> data
);

struct bind_complete
{
};
inline boost::system::error_code parse(boost::span<const unsigned char> data, bind_complete&)
{
    return detail::check_empty(data);
}

struct close_complete
{
};
inline boost::system::error_code parse(boost::span<const unsigned char> data, close_complete&)
{
    return detail::check_empty(data);
}

// struct command_complete_message
// {
//     // The command tag. This is usually a single word that identifies which SQL command was completed.
//     std::string_view tag;

//     void parse(parse_context& ctx) { tag = ctx.get_string(); }
// };

// struct backend_message
// {
//     union data_t
//     {
//         boost::system::error_code error;
//         authentication_request_message authentication_request;
//         backend_key_data_message backend_key_data;
//         bind_complete_message bind_complete;
//         close_complete_message close_complete;
//         command_complete_message command_complete;

//         data_t(boost::system::error_code v) noexcept : error(v) {}
//         data_t(authentication_request_message v) noexcept : authentication_request(v) {}
//         data_t(backend_key_data_message v) noexcept : backend_key_data(v) {}
//         data_t(bind_complete_message v) noexcept : bind_complete(v) {}
//         data_t(close_complete_message v) noexcept : close_complete(v) {}
//         data_t(command_complete_message v) noexcept : command_complete(v) {}
//     };

//     backend_message_type type;
//     data_t data;
// };

// // When the type is known
// template <class Message>
// inline boost::system::result<Message> parse_known_message(boost::span<const unsigned char> bytes)
// {
//     parse_context ctx(bytes);
//     Message res;
//     res.parse(ctx);
//     ctx.check_extra_bytes();
//     boost::system::error_code err = ctx.error();
//     if (err)
//         return err;
//     return res;
// }

// template <class Message>
// backend_message parse_message_impl(backend_message_type type, boost::span<const unsigned char> data)
// {
//     auto res = parse_known_message<Message>(data);
//     if (res.has_error())
//         return {backend_message_type::invalid, res.error()};
//     return {type, *res};
// }

// inline backend_message parse_message(unsigned char type, boost::span<const unsigned char> data)
// {
//     auto t = static_cast<backend_message_type>(type);
//     switch (t)
//     {
//     case backend_message_type::authentication_request:
//         return parse_message_impl<authentication_request_message>(t, data);
//     case backend_message_type::backend_key_data: return parse_message_impl<backend_key_data_message>(t,
//     data); case backend_message_type::bind_complete: return parse_message_impl<bind_complete_message>(t,
//     data); case backend_message_type::close_complete: return parse_message_impl<close_complete_message>(t,
//     data); case backend_message_type::command_complete: return
//     parse_message_impl<command_complete_message>(t, data); default:
//         return {backend_message_type::invalid,
//         boost::system::error_code(client_errc::protocol_value_error)};
//     }
// }

}  // namespace protocol
}  // namespace nativepg