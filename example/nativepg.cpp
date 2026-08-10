//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/describe.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <vector>

#include "nativepg/field_traits.hpp"
#include "nativepg/field_view.hpp"
#include "nativepg/protocol/common.hpp"

namespace describe = boost::describe;

namespace nativepg {

using parse_field_cb = std::error_code (*)(field_view, std::int32_t, protocol::format_code, void*);

// Describes a C++ field. An array of these describes a C++ struct
struct cpp_field_descriptor_erased
{
    // The field name to look up in the DB query
    std::string_view field_name;

    // Parses a field against the row struct
    parse_field_cb parse_fn;
};

// A function suitable for parse_field_cb
template <class Row, class FieldType, FieldType Row::* member>
std::error_code do_field_parse(field_view fv, std::int32_t type_oid, protocol::format_code code, void* row)
{
    return field_parse(fv, type_oid, code, static_cast<Row*>(row)->*member);
}

template <class Row, class FieldType, FieldType Row::* member>
struct cpp_field_descriptor
{
    std::string_view name;

    constexpr friend bool operator==(const cpp_field_descriptor&, const cpp_field_descriptor&) = default;
};

template <class T>
struct member_ptr_traits;
template <class C, class T>
struct member_ptr_traits<T C::*>
{
    using class_type = C;
    using field_type = T;
};

template <auto member>
constexpr auto make_cpp_descriptor(std::string_view name)
{
    using ClassType = typename member_ptr_traits<decltype(member)>::class_type;
    using FieldType = typename member_ptr_traits<decltype(member)>::field_type;
    return cpp_field_descriptor<ClassType, FieldType, member>{name};
}

template <class Row, template <class...> class ListType, class... D>
constexpr auto get_describe_descriptors_impl(ListType<D...>)
{
    return std::make_tuple(make_cpp_descriptor<D::pointer>(D::name)...);
}

template <class Row>
constexpr auto get_describe_descriptors()
{
    using row_members = describe::describe_members<Row, describe::mod_public | describe::mod_inherited>;
    return get_describe_descriptors_impl<Row>(row_members{});
}

template <class Row, class FieldType, FieldType Row::* member>
constexpr cpp_field_descriptor_erased erase_descriptor(cpp_field_descriptor<Row, FieldType, member> desc)
{
    return {.field_name = desc.name, .parse_fn = do_field_parse<Row, FieldType, member>};
}

constexpr struct erase_descriptors_t
{
    template <class... Descriptors>
    constexpr std::array<cpp_field_descriptor_erased, sizeof...(Descriptors)> operator()(
        Descriptors... descs
    ) const
    {
        return {{erase_descriptor(descs)...}};
    }

} erase_descriptors;

// template <class Row, class... FieldType>
// constexpr std::vector<cpp_field_descriptor_erased>

}  // namespace nativepg

using namespace nativepg;

struct myrow
{
    std::int32_t f3;
    std::string f1;
};
BOOST_DESCRIBE_STRUCT(myrow, (), (f3, f1))

struct manual_row
{
    int field1;
    float field2;
};

// constexpr auto metadata = std::make_tuple();
constexpr auto metadata = get_describe_descriptors<myrow>();
constexpr auto metadata2 = std::tuple{
    make_cpp_descriptor<&myrow::f3>("f3"),
    make_cpp_descriptor<&myrow::f1>("f1")
};
static_assert(metadata == metadata2);

constexpr auto descs = std::apply(erase_descriptors, metadata);

int main()
{
    // static_assert(expression, );
    // constexpr auto desc = get_describe_descriptors<myrow>();
    // constexpr auto erased_desc = erase_descriptor<class Row, class DescriptorWrapper>()
}