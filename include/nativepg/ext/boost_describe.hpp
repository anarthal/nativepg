//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_EXT_BOOST_DESCRIBE_HPP
#define NATIVEPG_EXT_BOOST_DESCRIBE_HPP

#include <boost/describe/members.hpp>

#include <tuple>

#include "nativepg/member_descriptor.hpp"

namespace nativepg {

template <class Row, template <class...> class ListType, class... D>
constexpr auto describe_descriptors_impl(ListType<D...>)
{
    return std::make_tuple(make_member_descriptor<D::pointer>(D::name)...);
}

template <class Row>
constexpr auto describe_descriptors()
{
    using namespace boost::describe;
    using row_members = describe_members<Row, mod_public | mod_inherited>;
    return describe_descriptors_impl<Row>(row_members{});
}

template <class Row>
struct boost_describe_tag
{
    static constexpr auto get() { return describe_descriptors<Row>(); }
};

}  // namespace nativepg

#endif
