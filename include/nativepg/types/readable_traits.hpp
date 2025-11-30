//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_TYPES_READABLE_TRAITS_HPP
#define NATIVEPG_TYPES_READABLE_TRAITS_HPP

#include <concepts>
#include <type_traits>

#include <boost/core/span.hpp>
#include <boost/system/error_code.hpp>

namespace nativepg {
namespace types {

template <typename Traits>
concept readable_traits =
    requires(boost::span<const std::byte> bytes,
             std::string_view text,
             typename Traits::value_type& out)
    {
        //
        // Required metadata
        //
        { Traits::supports_binary } -> std::convertible_to<bool>;
        { Traits::byte_len }        -> std::convertible_to<std::int32_t>;

        //
        // Required readable API
        //
        { Traits::parse_binary(bytes, out) }
            -> std::same_as<boost::system::error_code>;

        { Traits::parse_text(text, out) }
            -> std::same_as<boost::system::error_code>;
    };

}
}

#endif
