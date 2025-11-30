//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_TYPES_WRITEABLE_TYPE_HPP
#define NATIVEPG_TYPES_WRITEABLE_TYPE_HPP

#include <concepts>

#include <boost/system/error_code.hpp>

namespace nativepg {
namespace types {

    template <typename Traits>
    concept writeable_traits =
        requires(typename Traits::value_type& val,
                 boost::span<std::byte>& bytes,
                 std::string& text)
    {
        //
        // Required metadata
        //
        { Traits::supports_binary } -> std::convertible_to<bool>;
        { Traits::byte_len }        -> std::convertible_to<std::int32_t>;

        //
        // Required readable API
        //
        { Traits::serialize_binary(val, bytes) } -> std::same_as<boost::system::error_code>;
        { Traits::serialize_text(val, text) }   -> std::same_as<boost::system::error_code>;
    };

}
}

#endif
