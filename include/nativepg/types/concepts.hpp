//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_TYPES_CONCEPTS_HPP
#define NATIVEPG_TYPES_CONCEPTS_HPP

#include <concepts>

#include "traits.hpp"
#include "nullable.hpp"


namespace nativepg::types {

template<typename T>
concept UniquePtr = is_unique_ptr_v<T>;

template<typename T>
concept SharedPtr = is_shared_ptr_v<T>;

template<typename T>
concept WeakPtr = is_weak_ptr_v<T>;

template<typename T>
concept Optional = is_optional_v<T>;

template<typename T>
concept SmartPtr = is_smart_pointer_v<T>;

template<typename T>
concept RawPtr = is_raw_pointer_v<T>;

template<typename T>
concept GenericPtr = is_generic_pointer_v<T>;

template <typename T>
concept Nullable = is_nullable_type_v<T>;

}  // namespace nativepg::types

#endif  // NATIVEPG_TYPES_CONCEPTS_HPP
