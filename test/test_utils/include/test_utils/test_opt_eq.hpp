//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_TEST_OPT_EQ_HPP
#define NATIVEPG_TEST_OPT_EQ_HPP

#include <boost/assert/source_location.hpp>
#include <boost/core/lightweight_test.hpp>

#include <iostream>
#include <optional>
#include <ostream>
#include <type_traits>

namespace nativepg::test {

namespace detail {

template <class T>
struct optional_wrapper
{
    const std::optional<T>& value;

    constexpr friend bool operator==(const optional_wrapper& lhs, const optional_wrapper& rhs)
    {
        return lhs.value == rhs.value;
    }

    friend std::ostream& operator<<(std::ostream& os, optional_wrapper<T> v)
    {
        return v.value.has_value() ? (os << *v.value) : (os << "{nullopt}");
    }
};

}  // namespace detail

// std::optional doesn't have operator<<
template <class T>
bool test_opt_eq(
    const std::optional<T>& opt1,
    const std::optional<std::type_identity_t<T>>& opt2,
    boost::source_location loc = BOOST_CURRENT_LOCATION
)
{
    bool ok = BOOST_TEST_EQ(detail::optional_wrapper{opt1}, detail::optional_wrapper{opt2});
    if (!ok)
        std::cerr << "  Called from " << loc << std::endl;
    return ok;
}

}  // namespace nativepg::test

#endif
