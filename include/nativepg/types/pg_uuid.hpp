//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_TYPES_PG_UUID_HPP
#define NATIVEPG_TYPES_PG_UUID_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <stdexcept>

#include "nativepg/types/pg_type_traits.hpp"
#include "nativepg/types/basic_pg_value.hpp"

namespace nativepg {
namespace types {

template <>
struct pg_type_traits<std::array<std::uint8_t, 16>, pg_oid_type::uuid>
{
    using value_type = std::array<std::uint8_t, 16>;

    static constexpr pg_oid_type   oid_type  = pg_oid_type::uuid;
    static constexpr std::int32_t  byte_len  = 16;

    static constexpr std::string_view oid_name  = "UUID";
    static constexpr std::string_view type_name = "std::array<std::uint8_t,16>";

    static constexpr bool supports_binary = true;

    // ----- helpers for hex -----
    static constexpr char to_hex_digit(std::uint8_t v) noexcept
    {
        return (v < 10) ? static_cast<char>('0' + v)
                        : static_cast<char>('a' + (v - 10));
    }

    static std::uint8_t from_hex_digit(char c)
    {
        if (c >= '0' && c <= '9') return static_cast<std::uint8_t>(c - '0');
        if (c >= 'a' && c <= 'f') return static_cast<std::uint8_t>(c - 'a' + 10);
        if (c >= 'A' && c <= 'F') return static_cast<std::uint8_t>(c - 'A' + 10);
        throw std::runtime_error{"Invalid hex digit in UUID"};
    }

    // ----- TEXT ENCODE / DECODE -----
    static std::string encode_text(const value_type& v)
    {
        // Canonical 36-char form: 8-4-4-4-12
        std::string out;
        out.resize(36);

        std::size_t o = 0;
        for (std::size_t i = 0; i < 16; ++i)
        {
            if (i == 4 || i == 6 || i == 8 || i == 10)
                out[o++] = '-';

            std::uint8_t byte = v[i];
            out[o++] = to_hex_digit((byte >> 4) & 0x0F);
            out[o++] = to_hex_digit(byte & 0x0F);
        }

        return out;
    }

    static value_type decode_text(std::string_view sv)
    {
        // Accept canonical form with dashes, or just 32 hex digits
        value_type out{};
        std::size_t out_idx = 0;

        std::uint8_t high_nibble = 0;
        bool have_half_byte = false;

        for (char c : sv)
        {
            if (c == '-') continue; // ignore dashes

            std::uint8_t nibble = from_hex_digit(c);

            if (!have_half_byte)
            {
                high_nibble = nibble;
                have_half_byte = true;
            }
            else
            {
                if (out_idx >= out.size())
                    throw std::runtime_error{"Too many hex digits in UUID"};

                out[out_idx++] = static_cast<std::uint8_t>((high_nibble << 4) | nibble);
                have_half_byte = false;
            }
        }

        if (have_half_byte || out_idx != out.size())
            throw std::runtime_error{"Invalid UUID hex length"};

        return out;
    }

    // ----- BINARY ENCODE / DECODE -----
    static std::string encode_binary(const value_type& v)
    {
        std::string buf;
        buf.resize(16);
        for (std::size_t i = 0; i < 16; ++i)
        {
            buf[i] = static_cast<char>(v[i]);
        }
        return buf;
    }

    static value_type decode_binary(std::string_view sv)
    {
        if (sv.size() != 16)
            throw std::runtime_error{"uuid binary length != 16"};

        value_type out{};
        for (std::size_t i = 0; i < 16; ++i)
        {
            out[i] = static_cast<std::uint8_t>(
                static_cast<unsigned char>(sv[i])
            );
        }
        return out;
    }
};


// OID -> C++ type mapping
template <>
struct pg_type_from_oid<pg_oid_type::uuid>
{
    using type = std::array<std::uint8_t, 16>;
};


// Trait type
using pg_uuid_traits = pg_type_traits<
    pg_type_from_oid<pg_oid_type::uuid>::type,
    pg_oid_type::uuid
>;

// Value Type alias
using pg_uuid = basic_pg_value<pg_uuid_traits>;

} // namespace types
} // namespace nativepg

#endif // NATIVEPG_TYPES_PG_UUID_HPP
