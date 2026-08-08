//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_PROTOCOL_BIND_HPP
#define NATIVEPG_PROTOCOL_BIND_HPP

#include <boost/compat/function_ref.hpp>

#include <optional>
#include <span>
#include <string_view>
#include <system_error>
#include <vector>

#include "nativepg/protocol/common.hpp"
#include "nativepg/protocol/format_codes.hpp"

namespace nativepg {
namespace protocol {

// TODO: rename all this and wrap this
using serializable_ref = std::optional<
    boost::compat::function_ref<std::error_code(std::vector<unsigned char>&)>>;

struct bind
{
    // The name of the destination portal (an empty string selects the unnamed portal).
    std::string_view portal_name;

    // The name of the source prepared statement (an empty string selects the unnamed prepared statement).
    std::string_view statement_name;

    // The parameter format codes
    format_codes parameter_fmt_codes;

    // The actual parameters. The number of parameters must match the number of parameters required by the
    // query. An empty optional is serialized as a NULL value.
    std::span<const serializable_ref> parameters;

    // The result-column format codes.
    format_codes result_fmt_codes;
};
std::error_code serialize(const bind& msg, std::vector<unsigned char>& to);

struct bind_complete
{
};
inline std::error_code parse(std::span<const unsigned char> data, bind_complete&)
{
    return detail::check_empty(data);
}

}  // namespace protocol
}  // namespace nativepg

#endif
