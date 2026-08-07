//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_FIELD_TRAITS_HPP
#define NATIVEPG_FIELD_TRAITS_HPP

#include <concepts>
#include <cstdint>
#include <system_error>
#include <vector>

#include "nativepg/field_view.hpp"

namespace nativepg {

namespace detail {
struct is_unspecialized
{
};
}  // namespace detail

/**
 * Specialize this template to add parsing support for your own types.
 * Such types can then be used in structs to be passed to `into`, for example.
 *
 * The traits struct must declare the following functions
 * (have a look at the `parsable_field` concept for reference):
 *
 *  - is_compatible: determines whether the given Postgres type, identified
 *    by its OID, is compatible with your type. Invoked once per query,
 *    before parsing any data. Try to detect as much errors as possible
 *    here, as this function is invoked only once, while parsing
 *    is invoked once per row. Signature:
 *
 *    static std::error_code is_compatible(std::int32_t type_oid);
 *
 *  - parse_text: performs the actual parsing, for fields sent by the server
 *    using the text format. Returns an error if the value can't be represented
 *    in your type. type_oid is the OID that is_compatible accepted, and is
 *    relevant if your type accepts more than one. field_view is non-owning and
 *    can represent database NULLs - remember to check for these. Signature:
 *
 *    static std::error_code parse_text(field_view, std::int32_t type_oid, T&)
 *
 *  - parse_binary: same, but for fields sent using the binary format.
 *    Signature:
 *
 *    static std::error_code parse_binary(field_view, std::int32_t type_oid, T&)
 *
 */
template <class T>
struct parse_field_traits : detail::is_unspecialized
{
};

/**
 * Specialize this template to add serialization support for your own types.
 * Such types can then be used as parameters in `request`, for example.
 *
 * The traits struct must declare the following functions
 * (have a look at the `serializable_field` concept for reference):
 *
 *  - oid: the Postgres type OID that this C++ type corresponds to.
 *    This should match with how the value is serialized, especially
 *    when using the binary format. For example, a 2-byte integer type
 *    should be assigned the int2 type OID.
 *
 *  - serialize_text: serialized the value into a buffer, using the text
 *    format. If the value is not representable in the corresponding protocol
 *    type, an error can be returned. Signature:
 *
 *    static std::error_code serialize_text(const T& value, std::vector<unsigned char>& buffer)
 *
 *  - serialize_binary: same, but using the binary format. Signature:
 *
 *    static std::error_code serialize_binary(const T& value, std::vector<unsigned char>& buffer)
 */
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
        { parse_field_traits<T>::is_compatible(std::int32_t{}) } -> std::convertible_to<std::error_code>;

        // If you are seeing an error message pointing to this expression,
        // your parse_text function in the parse_field_traits specialization
        // for your type is missing or has an incorrect shape.
        {
            parse_field_traits<T>::parse_text(field_view{}, std::int32_t{}, value)
        } -> std::convertible_to<std::error_code>;

        // If you are seeing an error message pointing to this expression,
        // your parse_binary function in the parse_field_traits specialization
        // for your type is missing or has an incorrect shape.
        {
            parse_field_traits<T>::parse_binary(field_view{}, std::int32_t{}, value)
        } -> std::convertible_to<std::error_code>;
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
        { serialize_field_traits<T>::serialize_text(value, to) } -> std::convertible_to<std::error_code>;

        // If you are seeing an error message pointing to this expression,
        // your serialize_binary function in the serialize_field_traits specialization
        // for your type is missing or has an incorrect shape.
        { serialize_field_traits<T>::serialize_binary(value, to) } -> std::convertible_to<std::error_code>;
    };

// Now if you, as a user, want to add support for a type, you specialize any of these.
// But if you need to call the functionality here, don't invoke the traits structs directly,
// but use these functions, as they invoke concept checking.
template <parsable_field T>
std::error_code field_is_compatible(std::int32_t type_oid)
{
    return parse_field_traits<T>::is_compatible(type_oid);
}

template <parsable_field T>
std::error_code field_parse_text(field_view from, std::int32_t type_oid, T& to)
{
    return parse_field_traits<T>::parse_text(from, type_oid, to);
}

template <parsable_field T>
std::error_code field_parse_binary(field_view from, std::int32_t type_oid, T& to)
{
    return parse_field_traits<T>::parse_binary(from, type_oid, to);
}

template <serializable_field T>
inline constexpr std::int32_t field_serialize_oid = serialize_field_traits<T>::oid;

template <serializable_field T>
std::error_code field_serialize_text(const T& value, std::vector<unsigned char>& to)
{
    return serialize_field_traits<T>::serialize_text(value, to);
}

template <serializable_field T>
std::error_code field_serialize_binary(const T& value, std::vector<unsigned char>& to)
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
