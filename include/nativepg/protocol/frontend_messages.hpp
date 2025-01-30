//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_PROTOCOL_FRONTEND_MESSAGES_HPP
#define NATIVEPG_PROTOCOL_FRONTEND_MESSAGES_HPP

#include <boost/core/span.hpp>

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>

#include "nativepg/wire/serialization_context.hpp"

namespace nativepg {
namespace detail {

enum class format_code : std::int16_t
{
    text = 0,
    binary = 1
};

struct password_message
{
    static constexpr unsigned char message_type = static_cast<unsigned char>('p');

    // The password (encrypted, if requested). TODO: does this really have NULL terminator??
    std::string_view password;

    void serialize(serialization_context& ctx) const { ctx.add_string(password); }
};

struct query_message
{
    static constexpr unsigned char message_type = static_cast<unsigned char>('Q');

    // The query string itself.
    std::string_view query;

    void serialize(serialization_context& ctx) const { ctx.add_string(query); }
};

struct terminate_message
{
    static constexpr unsigned char message_type = static_cast<unsigned char>('X');

    void serialize(serialization_context& ctx) const {}
};

// Note: the below messages don't have a type code
struct ssl_request
{
    void serialize(serialization_context& ctx) const
    {
        // The SSL request code. The value is chosen to contain 1234 in the most significant 16 bits, and 5679
        // in the least significant 16 bits. (To avoid confusion, this code must not be the same as any
        // protocol version number.)
        ctx.add_integral(std::int32_t(80877103));
    }
};

struct startup_message
{
    // The database user name to connect as. Required; there is no default.
    std::string_view user;

    // The database to connect to. Defaults to the user name. Leave empty for no database
    std::string_view database;

    // Additional key/value settings
    boost::span<const std::pair<std::string_view, std::string_view>> params;

    void serialize(serialization_context& ctx) const
    {
        // The protocol version number. The most significant 16 bits are the major version number (3 for the
        // protocol described here). The least significant 16 bits are the minor version number (0 for the
        // protocol described here).
        ctx.add_integral(std::int32_t(196608));

        // Username
        ctx.add_string("user");
        ctx.add_string(user);

        // Database, if present
        if (!database.empty())
        {
            ctx.add_string("database");
            ctx.add_string(database);
        }

        // Rest of params
        for (auto param : params)
        {
            ctx.add_string(param.first);
            ctx.add_string(param.second);
        }

        // Terminator
        ctx.add_byte(0);
    }
};

// Note: this message does not have a type code
struct cancel_request
{
    // The process ID of the target backend.
    std::int32_t process_id;

    // The secret key for the target backend.
    std::int32_t secret_key;

    void serialize(serialization_context& ctx)
    {
        ctx.add_integral(static_cast<std::int32_t>(16));        // length
        ctx.add_integral(static_cast<std::int32_t>(80877102));  // cancel request code
        ctx.add_integral(process_id);
        ctx.add_integral(secret_key);
    }
};

}  // namespace detail
}  // namespace nativepg

#endif
