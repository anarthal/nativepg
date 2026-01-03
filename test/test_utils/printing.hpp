//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_TEST_PRINTING_HPP
#define NATIVEPG_TEST_PRINTING_HPP

#include <iosfwd>

namespace nativepg {

struct extended_error;

std::ostream& operator<<(std::ostream&, const extended_error&);

}  // namespace nativepg

#endif
