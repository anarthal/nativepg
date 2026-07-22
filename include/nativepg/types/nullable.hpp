//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_TYPES_NULLABLE_HPP
#define NATIVEPG_TYPES_NULLABLE_HPP

#include <type_traits>

#include "detail/traits.hpp"

namespace nativepg::types {

// For time being only considering std::optional as the nullable type, but can be extended to other types in
// the future.
template <typename T>
struct nullable_type_evaluator
{
    using CleanType = std::remove_cvref_t<T>;

    static_assert(
        !detail::is_raw_pointer_v<CleanType>,
        "Raw pointers are forbidden in NativePG as nullable type. Use std::optional."
    );

    static_assert(
        !detail::is_smart_pointer_v<CleanType>,
        "Smart pointers are not supported in NativePG as nullable type. Use std::optional."
    );

    // Only std::optional allowed!
    static constexpr bool value = detail::is_optional_v<CleanType>;
};

template <typename T>
inline constexpr bool is_nullable_type_v = nullable_type_evaluator<T>::value;


}  // namespace nativepg::types

#endif  // NATIVEPG_TYPES_NULLABLE_HPP
