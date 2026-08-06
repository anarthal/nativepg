//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_PROTOCOL_ASYNC_HPP
#define NATIVEPG_PROTOCOL_ASYNC_HPP

// Messages that may be received at any point (excluding notices, which share header with errors)

#include <system_error>

#include <cstdint>
#include <span>

namespace nativepg {
namespace protocol {

struct backend_key_data
{
    // The process ID of this backend.
    std::int32_t process_id;

    // The secret key of this backend.
    std::int32_t secret_key;
};
std::error_code parse(std::span<const unsigned char> data, backend_key_data& to);

struct notification_response
{
    // The process ID of the notifying backend process.
    std::int32_t process_id;

    // The name of the channel that the notify has been raised on.
    std::string_view channel_name;

    // The “payload” string passed from the notifying process.
    std::string_view payload;
};
std::error_code parse(std::span<const unsigned char> data, notification_response& to);

// Sent when a config parameter that might interest us changes value (e.g. character set)
struct parameter_status
{
    // The name of the run-time parameter being reported.
    std::string_view name;

    // The current value of the parameter.
    std::string_view value;
};
std::error_code parse(std::span<const unsigned char> data, parameter_status& to);

}  // namespace protocol
}  // namespace nativepg

#endif
