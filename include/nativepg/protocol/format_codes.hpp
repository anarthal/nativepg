//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_PROTOCOL_FORMAT_CODES_HPP
#define NATIVEPG_PROTOCOL_FORMAT_CODES_HPP

#include <boost/assert.hpp>

#include <span>

#include "nativepg/protocol/common.hpp"

namespace nativepg::protocol {

// The format codes for a set of parameters or result columns: either a single
// code, applied to all of them, or one code per element.
// Hand-rolled variant-like type.
class format_codes
{
    [[noreturn]] static void throw_invalid_argument();

public:
    enum class kind
    {
        // Applies the text format to all elements
        all_text,

        // Applies the binary format to all elements
        all_binary,

        // Applies format codes element per element
        list,
    };

    // Default constructor: a single format_code::text, applied to all elements
    format_codes() noexcept : kind_(kind::all_text) {}

    // Constructors from types (intentionally non-explicit)
    format_codes(format_code v) noexcept : kind_(v == format_code::text ? kind::all_text : kind::all_binary)
    {
    }
    format_codes(std::span<const format_code> v) noexcept : kind_(kind::list), list_(v) {}

    // Gets the kind
    kind type() const noexcept { return kind_; }

    // Gets the values as a list. Precondition: type() should match
    std::span<const format_code> get_list() const noexcept
    {
        BOOST_ASSERT(kind_ == kind::list);
        return list_;
    }

    // Checked getters. Throw if the actual kind doesn't match.
    std::span<const format_code> as_list() const
    {
        if (kind_ != kind::list)
            throw_invalid_argument();
        return list_;
    }

private:
    kind kind_;
    std::span<const format_code> list_;
};

}  // namespace nativepg::protocol

#endif
