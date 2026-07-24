//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_RESULTSETS_HPP
#define NATIVEPG_RESULTSETS_HPP

#include "nativepg/protocol/data_row.hpp"
#include "nativepg/responses/resultset_view.hpp"

namespace nativepg {

// A sequence of resultsets. Can accommodate the response to any number of SQL commands
class resultsets
{
    std::vector<detail::offsetted_field_description> field_descr_;
    std::vector<detail::offset_and_length> values_;
    std::vector<detail::resultset_descriptor> resultsets_;
    std::vector<unsigned char> data_;

public:
    resultsets() = default;

    // Makes the object empty, allowing for memory re-use
    void clear()
    {
        field_descr_.clear();
        values_.clear();
        resultsets_.clear();
        data_.clear();
    }

    // Part of the unstable API. Should only be used by
    // response authors.
    // TODO: move to cpp
    void add_row_description(const protocol::row_description& row_descr)
    {
        field_descr_.reserve(field_descr_.size() + row_descr.field_descriptions.size());
        for (const auto& descr : row_descr.field_descriptions)
        {
            field_descr_.push_back({
                .name = detail::insert_data(data_, descr.name),
                .table_oid = descr.table_oid,
                .column_attribute = descr.column_attribute,
                .type_oid = descr.type_oid,
                .type_length = descr.type_length,
                .type_modifier = descr.type_modifier,
                .fmt_code = descr.fmt_code,
            });
        }
    }

    // Part of the unstable API. Should only be used by
    // response authors.
    void add_row(const protocol::data_row& row)
    {
        values_.reserve(row.columns.size());
        for (const auto fv : row.columns)
        {
            if (fv.is_null())
                values_.push_back({.offset = 0u, .length = static_cast<std::size_t>(-1)});
            else
                values_.push_back(detail::insert_data(data_, fv.data()));
        }
    }

    void finish_resultset(
        std::size_t num_rows,
        std::size_t num_cols,
        command_info&& info,
        extended_error&& err
    )
    {
        const std::size_t num_values = num_cols * num_rows;

        BOOST_ASSERT(field_descr_.size() >= num_cols);
        BOOST_ASSERT(values_.size() >= num_values);

        resultsets_.push_back({
            .err = std::move(err),
            .info = std::move(info),
            .descr = {.offset = field_descr_.size() - num_cols, .length = num_cols  },
            .values = {.offset = values_.size() - num_values,    .length = num_values},
        });
    }

    class iterator
    {
        const detail::resultset_descriptor* it_{};
        const detail::offsetted_field_description* descr_{};
        const detail::offset_and_length* values_{};
        const unsigned char* data_{};

        friend class resultsets;
        iterator(
            const detail::resultset_descriptor* it,
            const detail::offsetted_field_description* descr,
            const detail::offset_and_length* values,
            const unsigned char* data
        ) noexcept
            : it_(it), descr_(descr), values_(values), data_(data)
        {
        }

    public:
        using value_type = resultset_view;
        using reference = resultset_view;  // prvalue, materialized on deref
        using pointer = resultset_view;
        using difference_type = std::ptrdiff_t;
        using iterator_category = std::random_access_iterator_tag;

        iterator() = default;

        reference operator*() const noexcept { return {descr_, values_, data_, it_}; }
        reference operator[](difference_type n) const noexcept { return {descr_, values_, data_, it_ + n}; }

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

    using value_type = resultset_view;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using reference = resultset_view;
    using const_reference = resultset_view;
    using const_iterator = iterator;

    // Iterators
    iterator begin() const noexcept
    {
        return {resultsets_.data(), field_descr_.data(), values_.data(), data_.data()};
    }
    iterator end() const noexcept
    {
        return {resultsets_.data() + resultsets_.size(), field_descr_.data(), values_.data(), data_.data()};
    }

    // Capacity
    size_type size() const noexcept { return resultsets_.size(); }
    bool empty() const noexcept { return resultsets_.empty(); }

    // Element access (all materialize a resultset_view by value)
    reference operator[](size_type i) const noexcept
    {
        return {field_descr_.data(), values_.data(), data_.data(), resultsets_.data() + i};
    }
    // TODO: at()
    reference front() const noexcept { return (*this)[0]; }
    reference back() const noexcept { return (*this)[size() - 1u]; }
};

}  // namespace nativepg

#endif
