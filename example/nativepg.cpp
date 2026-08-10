//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/describe.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
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

template <std::size_t N, class Row, class FieldType>
struct cpp_field_descriptor
{
    std::array<char, N> name;
    FieldType Row::* member;

    using row_type = Row;
    using field_type = FieldType;

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

template <std::size_t N, class Row, class FieldType>
constexpr auto make_cpp_descriptor(std::string_view name, FieldType Row::* member)
{
    cpp_field_descriptor<N, Row, FieldType> res{};
    std::copy(name.begin(), name.end(), res.name.begin());
    res.member = member;
    return res;
}

template <auto v>
struct wrapper
{
    static constexpr auto value = v;
};

template <std::size_t N, class Row, class FieldType>
constexpr auto make_cpp_descriptor2(std::string_view name, FieldType Row::* member)
{
    return wrapper<make_cpp_descriptor<std::size_t N>(std::string_view name, FieldType Row::* member)>{};
}

template <std::size_t N, class Row, class FieldType>
constexpr auto make_cpp_descriptor(const char (&str)[N], FieldType Row::* member)
{
    cpp_field_descriptor<N - 1, Row, FieldType> res{};
    std::copy(str, str + N - 1, res.name.begin());
    res.member = member;
    return res;
}

// template <class Row, template <class...> class ListType, class... D>
// constexpr auto get_describe_descriptors_impl(ListType<D...>)
// {
//     return std::make_tuple(make_cpp_descriptor<std::strlen(D::name)>(D::name, D::pointer)...);
// }

// template <class Row>
// constexpr auto get_describe_descriptors()
// {
//     using row_members = describe::describe_members<Row, describe::mod_public | describe::mod_inherited>;
//     return get_describe_descriptors_impl<Row>(row_members{});
// }

template <auto descriptor>
constexpr cpp_field_descriptor_erased erase_descriptor()
{
    using Row = decltype(descriptor)::row_type;
    using FieldType = decltype(descriptor)::field_type;
    return {
        .field_name = {descriptor.name.data(), descriptor.name.size()},
        .parse_fn = do_field_parse<Row, FieldType, descriptor.member>
    };
}

template <auto... descriptors>
constexpr std::array<cpp_field_descriptor_erased, sizeof...(descriptors)> erase_descriptors()
{
    return {{erase_descriptor<descriptors>()...}};
}

template <auto... descriptors>
constexpr std::array<cpp_field_descriptor_erased, sizeof...(descriptors)> erase_descriptors2(
    std::tuple<wrapper<descriptors>...>
)
{
    return {{erase_descriptor<descriptors>()...}};
}

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
// constexpr auto metadata = get_describe_descriptors<myrow>();
constexpr auto metadata2 = std::tuple{
    wrapper<make_cpp_descriptor("f3", &myrow::f3)>{},
    wrapper<make_cpp_descriptor("f1", &myrow::f1)>{}
};
// static_assert(metadata == metadata2);

constexpr auto descs = erase_descriptors2(metadata2);

int main()
{
    // static_assert(expression, );
    // constexpr auto desc = get_describe_descriptors<myrow>();
    // constexpr auto erased_desc = erase_descriptor<class Row, class DescriptorWrapper>()
}