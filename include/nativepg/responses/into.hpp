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

template <class MetaInfo>
struct into_handler
{
    using T = typename MetaInfo::type;
    std::vector<T>& vec;
    void operator()(T&& r) const { vec.push_back(std::move(r)); }
};

}  // namespace detail

// Resultset callback that output rows into a vector
// TODO: other allocators
// TODO: expand MetaInfo
template <class MetaInfo>
resultset_callback_t<MetaInfo, detail::into_handler<MetaInfo>> into(
    std::vector<typename MetaInfo::type>& vec,
    MetaInfo,
    command_info* out_info = nullptr
)
{
    return resultset_callback_t<MetaInfo, detail::into_handler<MetaInfo>>{
        detail::into_handler<MetaInfo>{vec},
        out_info
    };
}

}  // namespace nativepg

#endif
