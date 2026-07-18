//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_TYPES_NULLABLE_HPP
#define NATIVEPG_TYPES_NULLABLE_HPP

#include <iostream>
#include <concepts>
#include <type_traits>
#include <optional>
#include <functional>
#include <string>

namespace nativepg::types {


namespace detail {

// 1. Trait: Check if type supports nullptr operations AND is NOT a raw pointer
template<typename T>
struct is_nullable_ptr {
private:
    template<typename U>
    static auto test(int) -> std::bool_constant<
        requires(U u) {
        U(nullptr);
        u = nullptr;
        { u == nullptr } -> std::convertible_to<bool>;
        } && !std::is_pointer_v<U> // <-- CRITICAL: Explicitly rejects raw pointers
    >;
    template<typename> static auto test(...) -> std::false_type;
public:
    static constexpr bool value = decltype(test<T>(0))::value;
};

// 2. Trait: Check if type is a std::optional wrapper
template<typename T> struct is_optional : std::false_type {};
template<typename T> struct is_optional<std::optional<T>> : std::true_type {};

}

template<typename T>
inline constexpr bool is_nullable_ptr_v = detail::is_nullable_ptr<T>::value;

template<typename T>
inline constexpr bool is_optional_v = detail::is_optional<T>::value;

// 3. Combined Constexpr Variable Template
// For time being only considering std::optional as the nullable type, but can be extended to other types in the future.
// 3. Helper struct to house the static_assert compile guard safely
template<typename T>
struct nullable_type_evaluator {
    using CleanType = std::remove_cvref_t<T>;

    // CRITICAL COMPILE GUARD: Emits an explicit, clear error message if a raw pointer is encountered
    static_assert(!std::is_pointer_v<CleanType>,
                  "CRITICAL COMPILER ERROR: Raw pointers are forbidden in NativePG as nullable type. Use std::optional.");

    static_assert(!is_nullable_ptr_v<CleanType>,
                  "CRITICAL COMPILER ERROR: Smart pointers are not supported in NativePG as nullable type. Use std::optional.");

    static constexpr bool value = is_optional_v<CleanType>;
};


template<typename T>
inline constexpr bool is_nullable_type_v = nullable_type_evaluator<T>::value;


// 4. Combined C++20 Concept
template<typename T>
concept Nullable = is_nullable_type_v<T>;

// 5. Zero-Allocation, Zero-Exception unwrap_or() Helper Function
template<Nullable T, typename Fallback>
constexpr auto unwrap_or(T&& nullable_val, Fallback&& fallback_val) noexcept {
    // Determine the raw type of the underlying data item
    using RawType = std::remove_cvref_t<decltype(*std::forward<T>(nullable_val))>;

    // Enforce that the fallback type matches or can convert to the underlying data type
    static_assert(std::convertible_to<Fallback, RawType>, "Fallback type mismatch");

    // Using context conversion to bool (!). Works perfectly for pointers, unique_ptr, and std::optional.
    if (!nullable_val) {
        return std::reference_wrapper<const RawType>{std::forward<Fallback>(fallback_val)};
    }

    // Return a zero-cost reference wrapper pointing directly to the internal data
    return std::reference_wrapper<const RawType>{*std::forward<T>(nullable_val)};
}

// 6. Zero-Allocation, Zero-Exception assign_to Helper Function
// Returns true if an assignment occurred, false if skipped because it was null/empty.
template<Nullable T, typename Target>
constexpr bool assign_to(T&& nullable_val, Target& target_var) noexcept {
    using RawValueType = std::remove_cvref_t<decltype(*std::forward<T>(nullable_val))>;

    // Compile-time guard ensuring the unwrapped value can be assigned to the target
    static_assert(std::assignable_from<Target&, RawValueType>, "Incompatible assignment types");

    if (nullable_val) {
        // Perfect forward the dereferenced data straight into the target
        target_var = *std::forward<T>(nullable_val);
        return true;
    }

    return false; // Safely bypassed without throwing
}


}  // namespace nativepg::types

#endif  // NATIVEPG_TYPES_NULLABLE_HPP
