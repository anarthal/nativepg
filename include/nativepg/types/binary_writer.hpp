//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_TYPES_ENCODE_BINARY_HPP
#define NATIVEPG_TYPES_ENCODE_BINARY_HPP

#include <array>
#include <vector>
#include <span>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <concepts>
#include <iterator>
#include <cassert>

#include "nativepg/types/types.hpp"

namespace nativepg::types {

namespace detail {

    // ---------- Core: iterator-style big-endian encoder ----------
    template <class T, class OutIt>
    static OutIt encode_be(T value, OutIt it) {
        using U = std::make_unsigned_t<std::remove_cv_t<T>>;
        U u = static_cast<U>(value);

        if constexpr (std::endian::native == std::endian::big) {
            auto bytes = std::bit_cast<std::array<std::byte, sizeof(U)>>(u);
            for (std::byte b : bytes) *it++ = b;
        } else if constexpr (std::endian::native == std::endian::little) {
            auto bytes = std::bit_cast<std::array<std::byte, sizeof(U)>>(u);
            for (auto rit = bytes.rbegin(); rit != bytes.rend(); ++rit) *it++ = *rit;
        } else {
            for (int i = sizeof(U) - 1; i >= 0; --i)
                *it++ = std::byte{ static_cast<unsigned char>((u >> (i * 8)) & 0xFF) };
        }
        return it;
    };

}


template <class T, pg_oid_type Oid>
struct binary_writer
{
    using value_type = T;
    static constexpr pg_oid_type oid() noexcept { return Oid; }

    template <class OutIt>
    static OutIt write(T value, OutIt it) {
        return detail::encode_be(value, it);
    }

    // Convenience methods but not really necessary
    static auto to_array(T value) {
        std::array<std::byte, sizeof(std::make_unsigned_t<T>)> buf{};
        write(value, buf.begin());
        return buf;
    }

    static void to_span(T value, std::span<std::byte> out) {
        assert(out.size() >= sizeof(std::make_unsigned_t<T>));
        write(value, out.begin());
    }

    static std::vector<std::byte> to_vector(T value) {
        std::vector<std::byte> v;
        v.reserve(sizeof(std::make_unsigned_t<T>));
        write(value, std::back_inserter(v));
        return v;
    }
};


// ------------------------- BOOL -------------------------------------------
template<>
struct binary_writer<bool, pg_oid_type::bool_oid> {
    template <class OutIt>
    static OutIt write(bool v, OutIt it) {
        *it++ = static_cast<std::byte>(v ? 1 : 0);
        return it;
    }
};

// ------------------------- INT2 -------------------------------------------
template<>
struct binary_writer<uint16_t, pg_oid_type::int2> {
    template <class OutIt>
    static OutIt write(bool v, OutIt it) {
        return encode_be(v, it);
    }
};


}

#endif
