//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_DETAIL_FIELD_TRAITS_HPP
#define NATIVEPG_DETAIL_FIELD_TRAITS_HPP

#include <boost/system/error_code.hpp>

#include <concepts>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "nativepg/client_errc.hpp"
#include "nativepg/protocol/describe.hpp"

// TODO: at some point we will need to expose some customization
namespace nativepg::detail {

inline constexpr std::int32_t int2_oid = 21;
inline constexpr std::int32_t int4_oid = 23;
inline constexpr std::int32_t int8_oid = 20;

// --- Is a type compatible with what we get from DB?
// TODO: string diagnostics
template <class T>
struct field_is_compatible;

template <>
struct field_is_compatible<std::int16_t>
{
    static inline boost::system::error_code call(const protocol::field_description& desc)
    {
        return desc.type_oid == int2_oid ? boost::system::error_code() : client_errc::incompatible_field_type;
    }
};

template <>
struct field_is_compatible<std::int32_t>
{
    static inline boost::system::error_code call(const protocol::field_description& desc)
    {
        return desc.type_oid == int2_oid || desc.type_oid == int4_oid ? boost::system::error_code()
                                                                      : client_errc::incompatible_field_type;
    }
};

template <>
struct field_is_compatible<std::int64_t>
{
    static inline boost::system::error_code call(const protocol::field_description& desc)
    {
        return desc.type_oid == int2_oid || desc.type_oid == int4_oid || desc.type_oid == int8_oid
                   ? boost::system::error_code()
                   : client_errc::incompatible_field_type;
    }
};

// Strings can be used to parse any type
template <class T>
    requires std::assignable_from<T&, std::string_view>
struct field_is_compatible<T>
{
    static inline boost::system::error_code call(const protocol::field_description&)
    {
        return boost::system::error_code();
    }
};

// --- Parse
template <class T>
struct field_parse;

template <>
struct field_parse<std::int16_t>
{
    static boost::system::error_code call(
        std::optional<std::span<const unsigned char>> from,
        const protocol::field_description& desc,
        std::int16_t& to
    );
};

template <>
struct field_parse<std::int32_t>
{
    static boost::system::error_code call(
        std::optional<std::span<const unsigned char>> from,
        const protocol::field_description& desc,
        std::int32_t& to
    );
};

template <>
struct field_parse<std::int64_t>
{
    static boost::system::error_code call(
        std::optional<std::span<const unsigned char>> from,
        const protocol::field_description& desc,
        std::int64_t& to
    );
};

template <class T>
    requires std::assignable_from<T&, std::string_view>
struct field_parse<T>
{
    static inline boost::system::error_code call(
        std::optional<boost::span<const unsigned char>> from,
        const protocol::field_description&,
        T& to
    )
    {
        if (!from.has_value())
            return client_errc::unexpected_null;
        auto data = *from;
        to = std::string_view(reinterpret_cast<const char*>(data.data()), data.size());
        return {};
    }
};

}  // namespace nativepg::detail

#include "field_traits_datetime.hpp"

#endif
