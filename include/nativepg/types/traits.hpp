//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_TYPES_TRAITS_HPP
#define NATIVEPG_TYPES_TRAITS_HPP

#include <optional>
#include <type_traits>

namespace nativepg::types {

// std::unique_ptr
template<typename T>
struct is_unique_ptr : std::false_type {};
template<typename U>
struct is_unique_ptr<std::unique_ptr<U>> : std::true_type {};
template<typename T>
inline constexpr bool is_unique_ptr_v = is_unique_ptr<std::remove_cvref_t<T>>::value;

// std::shared_ptr
template<typename T>
struct is_shared_ptr : std::false_type {};
template<typename U>
struct is_shared_ptr<std::shared_ptr<U>> : std::true_type {};
template<typename T>
inline constexpr bool is_shared_ptr_v = is_shared_ptr<std::remove_cvref_t<T>>::value;

// std::weak_ptr
template<typename T>
struct is_weak_ptr : std::false_type {};
template<typename U>
struct is_weak_ptr<std::weak_ptr<U>> : std::true_type {};
template<typename T>
inline constexpr bool is_weak_ptr_v = is_weak_ptr<std::remove_cvref_t<T>>::value;

// std::optional
template <typename T>
struct is_optional : std::false_type {};
template <typename T>
struct is_optional<std::optional<T>> : std::true_type {};
template<typename T>
inline constexpr bool is_optional_v = is_optional<std::remove_cvref_t<T>>::value;

// Smart pointer check
template<typename T>
inline constexpr bool is_smart_pointer_v = is_shared_ptr_v<T> || is_unique_ptr_v<T> || is_weak_ptr_v<T>;

// Raw pointer check
template<typename T>
inline constexpr bool is_raw_pointer_v = std::is_pointer_v<T>;

// Generic pointer check
template<typename T>
inline constexpr bool is_generic_pointer_v = is_smart_pointer_v<T> || is_raw_pointer_v<T>;

}  // namespace nativepg::types

#endif  // NATIVEPG_TYPES_TRAITS_HPP
