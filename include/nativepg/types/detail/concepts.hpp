//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_TYPES_DETAIL_CONCEPTS_HPP
#define NATIVEPG_TYPES_DETAIL_CONCEPTS_HPP

#include <concepts>

#include "nativepg/types/nullable.hpp"
#include "nativepg/types/detail/traits.hpp"

namespace nativepg::types::detail {

template<typename T>
concept Optional = is_optional_v<T>;

template <typename T>
concept Nullable = is_nullable_type_v<T>;

}  // namespace nativepg::types::detail

#endif  // NATIVEPG_TYPES_DETAIL_CONCEPTS_HPP
