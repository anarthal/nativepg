//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_DETAIL_ROW_TRAITS_HPP
#define NATIVEPG_DETAIL_ROW_TRAITS_HPP

#include <boost/describe/members.hpp>
#include <boost/describe/modifiers.hpp>
#include <boost/mp11/list.hpp>

#include <array>
#include <string_view>

namespace nativepg::detail {

// TODO: support tuples, Boost.Pfr and C++26 reflection
namespace describe = boost::describe;

// Row traits: size
template <class T>
using row_members = describe::describe_members<T, describe::mod_public | describe::mod_inherited>;

template <class T>
inline constexpr std::size_t row_size_v = boost::mp11::mp_size<row_members<T>>::value;

// Row traits: name table
template <template <class...> class ListType, class... MemberDescriptor>
constexpr std::array<std::string_view, sizeof...(MemberDescriptor)> get_describe_names(
    ListType<MemberDescriptor...>
)
{
    return {MemberDescriptor::name...};
}

template <class T>
inline constexpr auto row_name_table_v = get_describe_names(row_members<T>{});

// Row traits: field types
template <class T, template <class...> class ListType, class... D>
constexpr auto row_field_types_impl(ListType<D...>)
{
    return boost::mp11::mp_list<
        std::remove_reference_t<decltype(std::declval<T>().*std::declval<D>().pointer)>...>{};
}

template <class T>
struct row_field_types
{
    using type = decltype(row_field_types_impl<T>(row_members<T>{}));
};

template <class T>
using row_field_types_t = typename row_field_types<T>::type;

// Row traits: for each member
template <class T, class F>
constexpr void for_each_member(T& row, F&& function)
{
    boost::mp11::mp_for_each<row_members<T>>([function, &row](auto D) { function(row.*D.pointer); });
}

}  // namespace nativepg::detail

#endif
