//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_PROTOCOL_PARSE_HPP
#define NATIVEPG_PROTOCOL_PARSE_HPP

#include <boost/core/span.hpp>
#include <boost/system/error_code.hpp>

#include <cstdint>
#include <string_view>

#include "nativepg/protocol/common.hpp"

namespace nativepg {
namespace protocol {

// TODO: rename parse functions
struct parse_t
{
    static constexpr unsigned char message_type = static_cast<unsigned char>('P');

    // The name of the destination prepared statement (an empty string selects the unnamed prepared
    // statement).
    std::string_view statement_name;

    // The query string to be parsed.
    std::string_view query;

    // Optional protocol format preference. Default text.
    format_code fmt = format_code::text;

    // Expected parameter data types, as OIDs. A zero OID leaves the type unspecified.
    boost::span<const std::int32_t> parameter_type_oids;
};
boost::system::error_code serialize(const parse_t& msg, std::vector<unsigned char>& to);

struct parse_complete
{
};
inline boost::system::error_code parse(boost::span<const unsigned char> data, parse_complete&)
{
    return detail::check_empty(data);
}

}  // namespace protocol
}  // namespace nativepg

#endif
