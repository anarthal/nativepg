//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_FIELD_TRAITS_HPP
#define NATIVEPG_FIELD_TRAITS_HPP

#include <boost/system/error_code.hpp>

#include <concepts>
#include <cstdint>
#include <vector>

#include "nativepg/field_view.hpp"
#include "nativepg/protocol/describe.hpp"

namespace nativepg {

namespace detail {
struct is_unspecialized
{
};
}  // namespace detail

// Base templates. Specialize these to add support for your own types.
// Specializations for the built-in type mappings live in the
// nativepg/detail/field_traits_*.hpp headers, included at the bottom of this file.
template <class T>
struct parse_field_traits : detail::is_unspecialized
{
};

template <class T>
struct serialize_field_traits : detail::is_unspecialized
{
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
        {
            serialize_field_traits<T>::serialize_text(value, to)
        } -> std::convertible_to<boost::system::error_code>;

        // If you are seeing an error message pointing to this expression,
        // your serialize_binary function in the serialize_field_traits specialization
        // for your type is missing or has an incorrect shape.
        {
            serialize_field_traits<T>::serialize_binary(value, to)
        } -> std::convertible_to<boost::system::error_code>;
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
boost::system::error_code field_serialize_text(const T& value, std::vector<unsigned char>& to)
{
    return serialize_field_traits<T>::serialize_text(value, to);
}

template <serializable_field T>
boost::system::error_code field_serialize_binary(const T& value, std::vector<unsigned char>& to)
{
    return serialize_field_traits<T>::serialize_binary(value, to);
}

}  // namespace nativepg

// Specializations for the built-in type mappings. They are included here, rather than
// at the top, because they require the base templates above to be visible.
// Mappings that require a third-party library are opt-in, and thus not included here:
// include nativepg/types/json.hpp, nativepg/types/numeric.hpp or nativepg/types/decimal.hpp
// to enable them.
#include "nativepg/detail/field_traits_base.hpp"
#include "nativepg/detail/field_traits_datetime.hpp"
#include "nativepg/detail/field_traits_nullable.hpp"

#endif  // NATIVEPG_FIELD_TRAITS_HPP
