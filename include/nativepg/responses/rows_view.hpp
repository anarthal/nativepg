//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_ROWS_VIEW_HPP
#define NATIVEPG_ROWS_VIEW_HPP

#include "nativepg/responses/detail/dynamic_utils.hpp"
#include "nativepg/responses/row_view.hpp"

namespace nativepg {

// A random-access, span-like view over the rows of a resultset.
// Values are stored flat (row-major), so each element is a row_view over a
// num_columns_-sized slice, materialized on access.
class rows_view
{
    std::span<const detail::offset_and_length> values_;
    std::size_t num_columns_{};
    const unsigned char* data_{};

    static inline row_view dereference(
        const detail::offset_and_length* p,
        std::size_t num_cols,
        const unsigned char* data
    )
    {
        return row_view(std::span<const detail::offset_and_length>{p, num_cols}, data);
    }

    static inline row_view dereference(
        const detail::offset_and_length* p,
        std::ptrdiff_t i,
        std::size_t num_cols,
        const unsigned char* data
    )
    {
        // TODO: this cast probably won't cause trouble, but double-check
        return dereference(p + i * static_cast<difference_type>(num_cols), num_cols, data);
    }

public:
    class iterator
    {
        const detail::offset_and_length* it_{};
        std::size_t num_columns_{};
        const unsigned char* data_{};

        friend class rows_view;
        iterator(
            const detail::offset_and_length* it,
            std::size_t num_columns,
            const unsigned char* data
        ) noexcept
            : it_(it), num_columns_(num_columns), data_(data)
        {
        }

    public:
        using value_type = row_view;
        using reference = row_view;  // prvalue, materialized on deref
        using pointer = row_view;
        using difference_type = std::ptrdiff_t;
        using iterator_category = std::random_access_iterator_tag;

        iterator() = default;

        reference operator*() const noexcept { return dereference(it_, num_columns_, data_); }
        reference operator[](difference_type n) const noexcept
        {
            return dereference(it_, n, num_columns_, data_);
        }

        iterator& operator++() noexcept
        {
            it_ += num_columns_;
            return *this;
        }
        iterator operator++(int) noexcept
        {
            auto copy = *this;
            ++*this;
            return copy;
        }
        iterator& operator--() noexcept
        {
            it_ -= num_columns_;
            return *this;
        }
        iterator operator--(int) noexcept
        {
            auto copy = *this;
            --*this;
            return copy;
        }
        iterator& operator+=(difference_type n) noexcept
        {
            it_ += n * static_cast<difference_type>(num_columns_);
            return *this;
        }
        iterator& operator-=(difference_type n) noexcept
        {
            it_ -= n * static_cast<difference_type>(num_columns_);
            return *this;
        }

        friend iterator operator+(iterator it, difference_type n) noexcept { return it += n; }
        friend iterator operator+(difference_type n, iterator it) noexcept { return it += n; }
        friend iterator operator-(iterator it, difference_type n) noexcept { return it -= n; }
        friend difference_type operator-(iterator lhs, iterator rhs) noexcept
        {
            return lhs.num_columns_ ? (lhs.it_ - rhs.it_) / static_cast<difference_type>(lhs.num_columns_)
                                    : 0;
        }

        friend bool operator==(iterator lhs, iterator rhs) noexcept { return lhs.it_ == rhs.it_; }
        friend std::strong_ordering operator<=>(iterator lhs, iterator rhs) noexcept
        {
            return lhs.it_ <=> rhs.it_;
        }
    };

    using value_type = row_view;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using reference = row_view;
    using const_reference = row_view;
    using const_iterator = iterator;

    rows_view() = default;
    // TODO: hide
    rows_view(
        std::span<const detail::offset_and_length> values,
        std::size_t num_columns,
        const unsigned char* data
    ) noexcept
        : values_(values), num_columns_(num_columns), data_(data)
    {
        BOOST_ASSERT(values.empty() || (values.size() % num_columns == 0u));
    }

    // Iterators
    iterator begin() const noexcept { return {values_.data(), num_columns_, data_}; }
    iterator end() const noexcept { return {values_.data() + size() * num_columns_, num_columns_, data_}; }

    // Capacity
    size_type size() const noexcept { return num_columns_ == 0u ? 0u : values_.size() / num_columns_; }
    bool empty() const noexcept { return size() == 0u; }
    size_type num_columns() const noexcept { return num_columns_; }

    // Element access (all materialize a row_view by value)
    reference operator[](size_type i) const noexcept
    {
        return row_view(values_.subspan(i * num_columns_, num_columns_), data_);
    }
    // TODO: at()
    reference front() const noexcept { return (*this)[0]; }
    reference back() const noexcept { return (*this)[size() - 1u]; }
};

}  // namespace nativepg

#endif
