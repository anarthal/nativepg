//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_INTO_HPP
#define NATIVEPG_INTO_HPP

#include <vector>

#include "nativepg/responses/resultset_callback.hpp"

namespace nativepg {

namespace detail {

template <class T>
struct into_handler
{
    std::vector<T>& vec;
    void operator()(T&& r) const { vec.push_back(std::move(r)); }
};

}  // namespace detail

// Resultset callback that output rows into a vector
// TODO: other allocators
template <class T>
resultset_callback_t<T, detail::into_handler<T>> into(std::vector<T>& vec, command_info* out_info = nullptr)
{
    return resultset_callback_t<T, detail::into_handler<T>>{detail::into_handler<T>{vec}, out_info};
}

}  // namespace nativepg

#endif
