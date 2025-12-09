//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_PARAMETER_REF_HPP
#define NATIVEPG_PARAMETER_REF_HPP

#include <boost/endian/conversion.hpp>
#include <boost/system/error_code.hpp>

#include <charconv>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <system_error>
#include <type_traits>

namespace nativepg {

namespace detail {

// There might be some duplication with types/ here.
// This is a temporal, minimal implementation supporting strings and ints while the proper one gets done.

// Does my type support binary serialization?
template <class T>
struct supports_binary : std::false_type
{
};

template <std::integral T>
struct supports_binary<T> : std::true_type
{
};

template <std::convertible_to<std::string_view> T>
struct supports_binary<T> : std::true_type
{
};

// Serialization functions.
// TODO: remove bool, char and charXY_t from this
template <std::integral T>
void serialize_text(const T& value, std::vector<unsigned char>& to)
{
    char buffer[512];
    auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    if (result.ec != std::errc{})
        throw std::system_error(std::make_error_code(result.ec));
    to.insert(to.end(), buffer, result.ptr);
}

template <std::integral T>
void serialize_binary(const T& value, std::vector<unsigned char>& to)
{
    constexpr std::size_t size = sizeof(T);
    auto offset = to.size();
    to.resize(offset + size);
    boost::endian::endian_store<T, size, boost::endian::order::big>(to.data() + offset, value);
}

inline void serialize_text(std::string_view value, std::vector<unsigned char>& to)
{
    to.insert(to.end(), value.begin(), value.end());
}

inline void serialize_binary(std::string_view value, std::vector<unsigned char>& to)
{
    to.insert(to.end(), value.begin(), value.end());
}

// Type OIDs when doing serialization
// clang-format off
template <class T> struct parameter_type_oid;
template <> struct parameter_type_oid<std::int16_t> { static inline constexpr std::int32_t value = 21; };
template <> struct parameter_type_oid<std::int32_t> { static inline constexpr std::int32_t value = 23; };
template <> struct parameter_type_oid<std::int64_t> { static inline constexpr std::int32_t value = 20; };
template <std::convertible_to<std::string_view> T> struct parameter_type_oid<T> { static inline constexpr std::int32_t value = 25; };
// clang-format on

// Access private functions in parameter_ref
struct parameter_ref_access;

}  // namespace detail

class parameter_ref
{
    using serialize_fn = void (*)(const void* param, std::vector<unsigned char>& buffer);

    template <class T>
    static void do_serialize_text(const void* param, std::vector<unsigned char>& buffer)
    {
        detail::serialize_text(*static_cast<const T*>(param), buffer);
    }

    template <class T>
    static void do_serialize_binary(const void* param, std::vector<unsigned char>& buffer)
    {
        detail::serialize_binary(*static_cast<const T*>(param), buffer);
    }

    template <class T>
    static serialize_fn make_serialize_binary()
    {
        if constexpr (detail::supports_binary<T>::value)
            return &do_serialize_binary<T>;
        else
            return nullptr;
    }

    const void* value_;
    serialize_fn text_;
    serialize_fn binary_;
    std::int32_t oid_;

    friend struct detail::parameter_ref_access;

public:
    template <class T>
        requires(!std::same_as<T, parameter_ref>)
    parameter_ref(const T& value) noexcept
        : value_(&value),
          text_(&do_serialize_text<T>),
          binary_(make_serialize_binary<T>()),
          oid_(detail::parameter_type_oid<T>::value)
    {
    }
};

namespace detail {

// Library-facing API
struct parameter_ref_access
{
    static bool supports_binary(const parameter_ref& v) { return v.binary_ != nullptr; }
    static void serialize_text(const parameter_ref& v, std::vector<unsigned char>& buffer)
    {
        v.text_(v.value_, buffer);
    }
    static void serialize_binary(const parameter_ref& v, std::vector<unsigned char>& buffer)
    {
        v.binary_(v.value_, buffer);
    }
    static std::int32_t type_oid(const parameter_ref& v) { return v.oid_; }
};

}  // namespace detail

}  // namespace nativepg

#endif
