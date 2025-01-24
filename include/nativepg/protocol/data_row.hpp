//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_PROTOCOL_DATA_ROW_HPP
#define NATIVEPG_PROTOCOL_DATA_ROW_HPP

#include <boost/core/span.hpp>
#include <boost/system/error_code.hpp>

#include <optional>

#include "nativepg/protocol/views.hpp"

namespace nativepg {
namespace protocol {
namespace detail {

// Collections impl
template <>
struct forward_traits<std::optional<boost::span<const unsigned char>>>
{
    static std::optional<boost::span<const unsigned char>> dereference(const unsigned char* data);
    static const unsigned char* advance(const unsigned char* data);
};

}  // namespace detail

struct data_row
{
    // The actual values. Contains a an optional<span<const unsigned char>> per column,
    // containing the serialized value, or an empty optional, if the field is NULL
    forward_parsing_view<std::optional<boost::span<const unsigned char>>> columns;
};
boost::system::error_code parse(boost::span<const unsigned char> data, data_row& to);

}  // namespace protocol
}  // namespace nativepg

#endif
