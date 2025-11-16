//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_TYPES_BASIC_PG_VALUE_HPP
#define NATIVEPG_TYPES_BASIC_PG_VALUE_HPP

#include <optional>
#include <utility>
#include <string_view>

#include "nativepg/types/pg_type_traits.hpp"


namespace nativepg {
namespace types {


// -----------------------------------------------------------------------------
// basic_pg_value – constexpr-friendly wrapper around pg_type_traits
// -----------------------------------------------------------------------------

template <typename T, pg_oid_type Oid,
          typename Traits = pg_type_traits<T, Oid>>
struct basic_pg_value
{
    using traits_type          = Traits;
    using value_type           = typename traits_type::value_type;
    using reference_type       = value_type&;
    using const_reference_type = const value_type&;

private:
    value_type value_{};    // trivial storage, constexpr-friendly

public:
    // ----- constructors (constexpr) -----
    constexpr basic_pg_value() = default;

    constexpr explicit basic_pg_value(const_reference_type v)
        : value_(v)
    {}

    constexpr explicit basic_pg_value(value_type&& v) noexcept
        : value_(static_cast<value_type&&>(v))
    {}

    // ----- meta info (all constexpr) -----
    static constexpr pg_oid_type oid() noexcept
    {
        return traits_type::oid_type;
    }

    static constexpr std::int32_t byte_length() noexcept
    {
        return traits_type::byte_len;
    }

    static constexpr std::string_view oid_name() noexcept
    {
        return traits_type::oid_name;
    }

    static constexpr std::string_view type_name() noexcept
    {
        return traits_type::type_name;
    }

    static constexpr bool supports_binary() noexcept
    {
        return traits_type::supports_binary;
    }

    // ----- accessors (constexpr) -----
    constexpr reference_type get() noexcept
    {
        return value_;
    }

    constexpr const_reference_type get() const noexcept
    {
        return value_;
    }

    constexpr operator const_reference_type() const noexcept
    {
        return value_;
    }

    // ----- runtime text encode/decode -----
    std::string encode_text() const
    {
        return traits_type::encode_text(value_);
    }

    static basic_pg_value from_text(std::string_view sv)
    {
        return basic_pg_value{traits_type::decode_text(sv)};
    }

    // ----- runtime binary encode/decode -----
    std::string encode_binary() const
    {
        if constexpr (traits_type::supports_binary)
        {
            return traits_type::encode_binary(value_);
        }
        else
        {
            throw std::logic_error{"Binary encoding not supported for this type"};
        }
    }

    static basic_pg_value from_binary(std::string_view sv)
    {
        if constexpr (traits_type::supports_binary)
        {
            return basic_pg_value{traits_type::decode_binary(sv)};
        }
        else
        {
            throw std::logic_error{"Binary decoding not supported for this type"};
        }
    }
};

}
}

#endif
