//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_FIELD_VIEW_HPP
#define NATIVEPG_FIELD_VIEW_HPP

#include <boost/assert.hpp>

#include <cstddef>
#include <span>

namespace nativepg {

// A view over a serialized field returned by the database.
// Might point to a (potentially empty) byte array, or be NULL
// (and these are two separate cases)
class field_view
{
    static constexpr unsigned char null_identifier{};

    const unsigned char* data_{&null_identifier};
    std::size_t size_{};

public:
    // Constructs a NULL
    constexpr field_view() = default;

    // Constructs a value
    constexpr field_view(std::span<const unsigned char> data) noexcept
        : data_(data.data()), size_(data.size())
    {
    }

    // Accessors
    constexpr bool is_null() const noexcept { return data_ == &null_identifier; }

    // data() will return empty if is_null() == true
    std::span<const unsigned char> data() const noexcept { return {data_, size_}; }
};

}  // namespace nativepg

#endif
