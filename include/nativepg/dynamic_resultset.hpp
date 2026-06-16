//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_DYNAMIC_RESULTSET_HPP
#define NATIVEPG_DYNAMIC_RESULTSET_HPP

// A structure similar to PGResult

#include <compare>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <span>
#include <string_view>
#include <vector>

#include "nativepg/field_view.hpp"
#include "nativepg/protocol/command_complete.hpp"
#include "nativepg/protocol/common.hpp"
#include "nativepg/protocol/data_row.hpp"
#include "nativepg/protocol/describe.hpp"
#include "nativepg/protocol/execute.hpp"
#include "nativepg/protocol/views.hpp"

namespace nativepg {

namespace detail {

struct offset_and_length
{
    std::size_t offset, length;

    field_view to_field_view(const unsigned char* data) const
    {
        // Lengths are limited to INT32_MAX, so we can use -1 to represent NULL
        if (length == static_cast<std::size_t>(-1))
            return field_view();
        return std::span<const unsigned char>{data + offset, length};
    }

    std::string_view to_string_view(const unsigned char* data) const
    {
        return {reinterpret_cast<const char*>(data) + offset, length};
    }
};

// Like protocol::field_description, but strings are offset based
struct offsetted_field_description
{
    offset_and_length name;
    std::int32_t table_oid;
    std::int16_t column_attribute;
    std::int32_t type_oid;
    std::int16_t type_length;
    std::int32_t type_modifier;
    protocol::format_code fmt_code;

    protocol::field_description to_field_description(const unsigned char* data) const
    {
        return {
            .name = name.to_string_view(data),
            .table_oid = table_oid,
            .column_attribute = column_attribute,
            .type_oid = type_oid,
            .type_length = type_length,
            .type_modifier = type_modifier,
            .fmt_code = fmt_code,
        };
    }
};

}  // namespace detail

// A random-access, span-like view over a range of field descriptions.
// Elements are materialized on access.
class field_descriptions_view
{
    std::span<const detail::offsetted_field_description> descrs_;
    const unsigned char* data_{};

public:
    class iterator
    {
        const detail::offsetted_field_description* it_{};
        const unsigned char* data_{};

        friend class field_descriptions_view;
        iterator(const detail::offsetted_field_description* it, const unsigned char* data) noexcept
            : it_(it), data_(data)
        {
        }

    public:
        using value_type = protocol::field_description;
        using reference = protocol::field_description;  // prvalue, materialized on deref
        using pointer = protocol::field_description;
        using difference_type = std::ptrdiff_t;
        using iterator_category = std::random_access_iterator_tag;

        iterator() = default;

        reference operator*() const { return it_->to_field_description(data_); }
        reference operator[](difference_type n) const { return it_[n].to_field_description(data_); }

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

    using value_type = protocol::field_description;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using reference = protocol::field_description;
    using const_reference = protocol::field_description;
    using const_iterator = iterator;

    field_descriptions_view() = default;
    field_descriptions_view(
        std::span<const detail::offsetted_field_description> descr,
        const unsigned char* data
    ) noexcept
        : descrs_(descr), data_(data)
    {
    }

    // Iterators
    iterator begin() const noexcept { return {descrs_.data(), data_}; }
    iterator end() const noexcept { return {descrs_.data() + descrs_.size(), data_}; }

    // Capacity
    size_type size() const noexcept { return descrs_.size(); }
    bool empty() const noexcept { return descrs_.empty(); }

    // Element access (all materialize a field_description by value)
    reference operator[](size_type i) const { return descrs_[i].to_field_description(data_); }
    // TODO: at()
    reference front() const { return descrs_.front().to_field_description(data_); }
    reference back() const { return descrs_.back().to_field_description(data_); }
};

// A random-access, span-like view over a row's values.
// Elements are materialized on access. A NULL value is represented as an empty optional.
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
            // TODO: this is UB when num_columns is zero
            return (lhs.it_ - rhs.it_) / static_cast<difference_type>(lhs.num_columns_);
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

class dynamic_resultset
{
    std::vector<detail::offsetted_field_description> field_descr_;
    std::vector<detail::offset_and_length> values_;
    std::vector<unsigned char> data_;
    detail::offset_and_length command_complete_tag_{};
    bool portal_suspended_{false};

    detail::offset_and_length insert_data(std::span<const unsigned char> value)
    {
        // Data coming from the server fulfills this assertion by protocol design
        BOOST_ASSERT(value.size() != static_cast<std::size_t>(-1));
        detail::offset_and_length res{.offset = data_.size(), .length = value.size()};
        data_.insert(data_.end(), value.begin(), value.end());
        return res;
    }

    detail::offset_and_length insert_data(std::string_view value)
    {
        return insert_data(
            std::span<const unsigned char>{reinterpret_cast<const unsigned char*>(value.data()), value.size()}
        );
    }

public:
    dynamic_resultset() = default;

    void clear()
    {
        field_descr_.clear();
        values_.clear();
        data_.clear();
        command_complete_tag_ = {};
        portal_suspended_ = false;
    }

    // TODO: these methods should be private?
    void add_row_description(const protocol::row_description& row_descr)
    {
        field_descr_.clear();
        for (const auto& descr : row_descr.field_descriptions)
        {
            field_descr_.push_back({
                .name = insert_data(descr.name),
                .table_oid = descr.table_oid,
                .column_attribute = descr.column_attribute,
                .type_oid = descr.type_oid,
                .type_length = descr.type_length,
                .type_modifier = descr.type_modifier,
                .fmt_code = descr.fmt_code,
            });
        }
    }

    void add_row(const protocol::data_row& row)
    {
        // TODO: I think this assert can technically trigger,
        // but it may not be this class' responsibility
        BOOST_ASSERT(row.columns.size() == field_descr_.size());
        for (const auto fv : row.columns)
        {
            if (fv.is_null())
                values_.push_back({.offset = 0u, .length = static_cast<std::size_t>(-1)});
            else
                values_.push_back(insert_data(fv.data()));
        }
    }

    void add_command_complete(protocol::command_complete msg)
    {
        command_complete_tag_ = insert_data(msg.tag);
    }

    void add_portal_suspended(protocol::portal_suspended) { portal_suspended_ = true; }

    field_descriptions_view field_descriptions() const noexcept { return {field_descr_, data_.data()}; }
    rows_view rows() const noexcept { return {values_, field_descr_.size(), data_.data()}; }

    // This is equivalent to PQcmdStatus
    // TODO: this tag is what libpq uses for PQcmdTuples and PQoidValue,
    // implement functions to parse it
    std::string_view command_complete_tag() const noexcept
    {
        return command_complete_tag_.to_string_view(data_.data());
    }
    bool portal_suspended() const noexcept { return portal_suspended_; }
};

}  // namespace nativepg

#endif
