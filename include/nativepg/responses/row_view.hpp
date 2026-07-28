//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_ROW_VIEW_HPP
#define NATIVEPG_ROW_VIEW_HPP

#include <span>

#include "nativepg/responses/detail/dynamic_utils.hpp"

namespace nativepg {

// A random-access, span-like view over a row's values.
// Elements are materialized on access.
class row_view
{
    std::span<const detail::offset_and_length> values_;
    const unsigned char* data_{};

public:
    class iterator
    {
        const detail::offset_and_length* it_{};
        const unsigned char* data_{};

        friend class row_view;
        iterator(const detail::offset_and_length* it, const unsigned char* data) noexcept
            : it_(it), data_(data)
        {
        }

    public:
        using value_type = field_view;
        using reference = field_view;  // prvalue, materialized on deref
        using pointer = field_view;
        using difference_type = std::ptrdiff_t;
        using iterator_category = std::random_access_iterator_tag;

        iterator() = default;

        reference operator*() const { return it_->to_field_view(data_); }
        reference operator[](difference_type n) const { return it_[n].to_field_view(data_); }

        iterator& operator++() noexcept
        {
            ++it_;
            return *this;
        }
        iterator operator++(int) noexcept
        {
            auto copy = *this;
            ++it_;
            return copy;
        }
        iterator& operator--() noexcept
        {
            --it_;
            return *this;
        }
        iterator operator--(int) noexcept
        {
            auto copy = *this;
            --it_;
            return copy;
        }
        iterator& operator+=(difference_type n) noexcept
        {
            it_ += n;
            return *this;
        }
        iterator& operator-=(difference_type n) noexcept
        {
            it_ -= n;
            return *this;
        }

        friend iterator operator+(iterator it, difference_type n) noexcept { return it += n; }
        friend iterator operator+(difference_type n, iterator it) noexcept { return it += n; }
        friend iterator operator-(iterator it, difference_type n) noexcept { return it -= n; }
        friend difference_type operator-(iterator lhs, iterator rhs) noexcept { return lhs.it_ - rhs.it_; }

        friend bool operator==(iterator lhs, iterator rhs) noexcept { return lhs.it_ == rhs.it_; }
        friend std::strong_ordering operator<=>(iterator lhs, iterator rhs) noexcept
        {
            return lhs.it_ <=> rhs.it_;
        }
    };

    using value_type = field_view;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using reference = field_view;
    using const_reference = field_view;
    using const_iterator = iterator;

    row_view() = default;
    // TODO: hide
    row_view(std::span<const detail::offset_and_length> values, const unsigned char* data) noexcept
        : values_(values), data_(data)
    {
    }

    // Iterators
    iterator begin() const noexcept { return {values_.data(), data_}; }
    iterator end() const noexcept { return {values_.data() + values_.size(), data_}; }

    // Capacity
    size_type size() const noexcept { return values_.size(); }
    bool empty() const noexcept { return values_.empty(); }

    // Element access (all materialize a value by value; NULL becomes an empty optional)
    reference operator[](size_type i) const { return values_[i].to_field_view(data_); }
    // TODO: at()
    reference front() const { return values_.front().to_field_view(data_); }
    reference back() const { return values_.back().to_field_view(data_); }
};

}  // namespace nativepg

#endif
