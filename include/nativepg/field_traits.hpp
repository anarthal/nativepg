//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_FIELD_TRAITS_HPP
#define NATIVEPG_FIELD_TRAITS_HPP

#include <boost/assert.hpp>
#include <boost/system/error_code.hpp>

#include <concepts>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "nativepg/field_view.hpp"
#include "nativepg/protocol/describe.hpp"
#include "nativepg/types/base.hpp"

namespace nativepg {

namespace detail {
struct is_unspecialized
{
};
}  // namespace detail

// Base template
template <class T>
struct parse_field_traits : detail::is_unspecialized
{
};

template <class T>
struct serialize_field_traits : detail::is_unspecialized
{
};

// Built-in types (demonstration)
inline constexpr std::int32_t int2_oid = 21;
inline constexpr std::int32_t text_oid = 25;
inline constexpr std::int32_t bpchar_oid = 1042;
inline constexpr std::int32_t varchar_oid = 1043;
inline constexpr std::int32_t name_oid = 19;

template <>
struct parse_field_traits<std::int16_t>
{
    static inline boost::system::error_code is_compatible(const protocol::field_description& desc)
    {
        if (desc.type_oid == int2_oid)
            return boost::system::error_code{};

        return client_errc::incompatible_field_type;
    }

    static inline boost::system::error_code parse(
        field_view from,
        const protocol::field_description& desc,
        std::int16_t& to
    )
    {
        if (from.is_null())
            return client_errc::unexpected_null;
        BOOST_ASSERT(desc.type_oid == int2_oid);
        return desc.fmt_code == protocol::format_code::text ? types::parse_text_int(from, to)
                                                            : types::parse_binary_int(from, to);
    }
};

template <>
struct serialize_field_traits<std::int16_t>
{
    static inline constexpr std::int32_t oid = int2_oid;

    void serialize_text(std::int16_t value, std::vector<unsigned char>& to)
    {
        return types::serialize_text_int(value, to);
    }

    void serialize_binary(std::int16_t value, std::vector<unsigned char>& to)
    {
        return types::serialize_binary_int(value, to);
    }
};

// String POC. When parsing, only owning strings are allowed
template <class Traits, class Alloc>
struct parse_field_traits<std::basic_string<char, Traits, Alloc>>
{
    static inline boost::system::error_code is_compatible(const protocol::field_description& desc)
    {
        if (desc.type_oid == text_oid || desc.type_oid == varchar_oid || desc.type_oid == name_oid ||
            desc.type_oid == bpchar_oid)
            return boost::system::error_code{};

        return client_errc::incompatible_field_type;
    }

    static inline boost::system::error_code parse(
        field_view from,
        const protocol::field_description& desc,
        std::basic_string<char, Traits, Alloc>& to
    )
    {
        if (from.is_null())
            return client_errc::unexpected_null;
        BOOST_ASSERT(
            desc.type_oid == text_oid || desc.type_oid == varchar_oid || desc.type_oid == name_oid ||
            desc.type_oid == bpchar_oid
        );
        return desc.fmt_code == protocol::format_code::text ? types::parse_text_text(from, to)
                                                            : types::parse_binary_text(from, to);
    }
};

// However, when serializing, anything convertible to std::string_view should be considered
// a string
template <std::convertible_to<std::string_view> T>
struct serialize_field_traits<T>
{
    static inline constexpr std::int32_t oid = text_oid;

    // TODO: this should return an error code
    static void serialize_text(std::string_view value, std::vector<unsigned char>& to)
    {
        return types::serialize_text_text(value, to);
    }

    static void serialize_binary(std::string_view value, std::vector<unsigned char>& to)
    {
        return types::serialize_binary_text(value, to);
    }
};

// A type satisfies this concept if it may be used as a target
// for field parsing. For instance, as a member in a struct to be used
// with into().
template <class T>
concept parsable_field =
    // If this concept is not satisfied, your type does not have the required
    // parse_field_traits specialization. It is currently not supported for parsing.
    !std::derived_from<parse_field_traits<T>, detail::is_unspecialized> &&

    requires(T& value) {
        // If you are seeing an error message pointing to this expression,
        // your is_compatible function in the parse_field_traits specialization
        // for your type is missing or has an incorrect shape.
        {
            parse_field_traits<T>::is_compatible(protocol::field_description{})
        } -> std::convertible_to<boost::system::error_code>;

        // If you are seeing an error message pointing to this concept,
        // your parse function in the parse_field_traits specialization
        // for your type is missing or has an incorrect shape.
        {
            parse_field_traits<T>::parse(field_view{}, protocol::field_description{}, value)
        } -> std::convertible_to<boost::system::error_code>;
    };

template <class T>
concept serializable_field =
    // If this concept is not satisfied, your type does not have the required
    // serialize_field_traits specialization. It is currently not supported for
    // being serialized as a SQL parameter.
    !std::derived_from<serialize_field_traits<T>, detail::is_unspecialized> &&

    requires(const T& value, std::vector<unsigned char>& to) {
        // If you are seeing an error message pointing to this expression,
        // your serialize_field_traits specialization is missing an oid static member,
        // or it has the wrong type.
        { serialize_field_traits<T>::oid } -> std::convertible_to<std::int32_t>;

        // If you are seeing an error message pointing to this expression,
        // your serialize_text function in the serialize_field_traits specialization
        // for your type is missing or has an incorrect shape.
        serialize_field_traits<T>::serialize_text(value, to);

        // If you are seeing an error message pointing to this expression,
        // your serialize_binary function in the serialize_field_traits specialization
        // for your type is missing or has an incorrect shape.
        serialize_field_traits<T>::serialize_binary(value, to);
    };

// Now if you, as a user, want to add support for a type, you specialize any of these.
// But if you need to call the functionality here, don't invoke the traits structs directly,
// but use these functions, as they invoke concept checking.
template <parsable_field T>
boost::system::error_code field_is_compatible(const protocol::field_description& desc)
{
    return parse_field_traits<T>::is_compatible(desc);
}

template <parsable_field T>
boost::system::error_code field_parse(field_view from, const protocol::field_description& desc, T& to)
{
    return parse_field_traits<T>::parse(from, desc, to);
}

template <serializable_field T>
inline constexpr std::int32_t field_serialize_oid = serialize_field_traits<T>::oid;

template <serializable_field T>
void field_serialize_text(const T& value, std::vector<unsigned char>& to)
{
    return serialize_field_traits<T>::serialize_text(value, to);
}

template <serializable_field T>
void field_serialize_binary(const T& value, std::vector<unsigned char>& to)
{
    return serialize_field_traits<T>::serialize_binary(value, to);
}

}  // namespace nativepg

#endif  // NATIVEPG_TYPES_HPP
