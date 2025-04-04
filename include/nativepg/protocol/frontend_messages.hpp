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

struct query_message
{
    static constexpr unsigned char message_type = static_cast<unsigned char>('Q');

    // The query string itself.
    std::string_view query;

    void serialize(serialization_context& ctx) const { ctx.add_string(query); }
};

}  // namespace detail
}  // namespace nativepg

#endif
