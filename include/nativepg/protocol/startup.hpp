//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_PROTOCOL_STARTUP_HPP
#define NATIVEPG_PROTOCOL_STARTUP_HPP

#include <boost/core/span.hpp>
#include <boost/system/error_code.hpp>

#include <optional>
#include <string_view>
#include <vector>

#include "nativepg/protocol/common.hpp"
#include "nativepg/protocol/views.hpp"

namespace nativepg {
namespace protocol {
namespace detail {

// Collections
template <>
struct forward_traits<std::string_view>
{
    static std::string_view dereference(const unsigned char* data);
    static const unsigned char* advance(const unsigned char* data);
};

}  // namespace detail

struct startup_message
{
    // The database user name to connect as. Required; there is no default.
    std::string_view user;

    // The database to connect to. Defaults to the user name. Leave empty for no database
    std::optional<std::string_view> database;

    // Additional key/value settings
    boost::span<const std::pair<std::string_view, std::string_view>> params;
};
boost::system::error_code serialize(const startup_message& msg, std::vector<unsigned char>& to);

struct password
{
    // The password (encrypted, if requested). TODO: does this really have NULL terminator??
    std::string_view password;
};
boost::system::error_code serialize(const password& msg, std::vector<unsigned char>& to);

// Authentication messages require parsing the following 4 bytes, too
struct authentication_ok
{
};
inline boost::system::error_code parse(boost::span<const unsigned char> data, authentication_ok&)
{
    return detail::check_empty(data);
}

struct authentication_kerberos_v5
{
};
inline boost::system::error_code parse(boost::span<const unsigned char> data, authentication_kerberos_v5&)
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

struct authentication_gss
{
};
inline boost::system::error_code parse(boost::span<const unsigned char> data, authentication_gss&)
{
    return detail::check_empty(data);
}

struct authentication_gss_continue
{
    // GSSAPI or SSPI authentication data.
    boost::span<const unsigned char> data;
};
inline boost::system::error_code parse(boost::span<const unsigned char> data, authentication_gss_continue& to)
{
    to.data = data;
    return {};
}

struct authentication_sspi
{
};
inline boost::system::error_code parse(boost::span<const unsigned char> data, authentication_sspi&)
{
    return detail::check_empty(data);
}

struct authentication_sasl
{
    // List of SASL authentication mechanisms, in the server's order of preference
    forward_parsing_view<std::string_view> mechanisms;
};
boost::system::error_code parse(boost::span<const unsigned char> data, authentication_sasl&);

struct authentication_sasl_continue
{
    // SASL data, specific to the SASL mechanism being used.
    boost::span<const unsigned char> data;
};
inline boost::system::error_code parse(
    boost::span<const unsigned char> data,
    authentication_sasl_continue& to
)
{
    to.data = data;
    return {};
}

struct authentication_sasl_final
{
    // SASL outcome "additional data", specific to the SASL mechanism being used.
    boost::span<const unsigned char> data;
};
inline boost::system::error_code parse(boost::span<const unsigned char> data, authentication_sasl_final& to)
{
    to.data = data;
    return {};
}

// TODO: do we want to expose a fn to parse any authentication message? (they share message code, type is
// determined with the following bytes)

struct negotiate_protocol_version
{
    // Newest minor protocol version supported by the server for the major protocol version requested by the
    // client.
    std::int32_t minor_version;

    // Options not recognized by the server
    forward_parsing_view<std::string_view> non_recognized_options;
};
boost::system::error_code parse(boost::span<const unsigned char> data, negotiate_protocol_version& to);

}  // namespace protocol
}  // namespace nativepg

#endif
