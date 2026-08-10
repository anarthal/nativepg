//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_MEMBER_DESCRIPTOR_HPP
#define NATIVEPG_MEMBER_DESCRIPTOR_HPP

#include <string_view>

namespace nativepg {

namespace detail {

// Extract member pointer types
template <class T>
struct member_ptr_traits;

template <class C, class T>
struct member_ptr_traits<T C::*>
{
    using class_type = C;
    using field_type = T;
};

}  // namespace detail

template <class Row, class FieldType, FieldType Row::* member>
struct member_descriptor
{
    std::string_view name;

    using row_type = Row;
    using field_type = FieldType;

    constexpr friend bool operator==(const member_descriptor&, const member_descriptor&) = default;
};

template <auto member>
constexpr auto make_member_descriptor(std::string_view name)
{
    using ClassType = typename detail::member_ptr_traits<decltype(member)>::class_type;
    using FieldType = typename detail::member_ptr_traits<decltype(member)>::field_type;
    return member_descriptor<ClassType, FieldType, member>{name};
}

}  // namespace nativepg

#endif
