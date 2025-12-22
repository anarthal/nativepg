//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_DETAIL_DISPOSITION_HPP
#define NATIVEPG_DETAIL_DISPOSITION_HPP

#include <boost/asio/disposition.hpp>

#include "nativepg/extended_error.hpp"

// Make extended_error a disposition
namespace boost::asio {

template <>
struct disposition_traits<nativepg::extended_error>
{
    static inline bool not_an_error(const nativepg::extended_error& d) noexcept { return !d.code.failed(); }

    static void throw_exception(const nativepg::extended_error& d);

    static std::exception_ptr to_exception_ptr(const nativepg::extended_error& d) noexcept;
};

}  // namespace boost::asio

#endif
