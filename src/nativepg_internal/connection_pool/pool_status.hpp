//
// Copyright (c) 2019-2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_POOL_STATUS_HPP
#define NATIVEPG_POOL_STATUS_HPP

namespace nativepg::detail {

enum class pool_status
{
    initial,
    running,
    cancelled,
};

}  // namespace nativepg::detail

#endif
