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

#include "nativepg/command_info.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/field_view.hpp"
#include "nativepg/protocol/common.hpp"
#include "nativepg/protocol/data_row.hpp"
#include "nativepg/protocol/describe.hpp"
#include "nativepg/protocol/views.hpp"

// TODO: separate compilation

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

// An owning, random-access range of field descriptions (one per column).
// Elements are materialized on access.
class field_descriptions
{
    std::vector<detail::offsetted_field_description> field_descr_;
    std::vector<unsigned char> data_;

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
    using value_type = protocol::field_description;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using reference = protocol::field_description;
    using const_reference = protocol::field_description;
    using iterator = field_descriptions_view::iterator;  // TODO
    using const_iterator = iterator;

    field_descriptions() = default;

    // The elements and their referenced data are owned by us, so the view's
    // pointers stay valid after the temporary view is gone.
    field_descriptions_view as_view() const noexcept { return {field_descr_, data_.data()}; }

    operator field_descriptions_view() const noexcept { return as_view(); }

    // Removes all data, allowing for memory re-use
    void clear()
    {
        field_descr_.clear();
        data_.clear();
    }

    // Part of the unstable API. Should only be used by
    // response authors.
    void assign(const protocol::row_description& row_descr)
    {
        clear();
        field_descr_.reserve(row_descr.field_descriptions.size());
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

    // Iterators
    iterator begin() const noexcept { return as_view().begin(); }
    iterator end() const noexcept { return as_view().end(); }

    // Capacity
    size_type size() const noexcept { return field_descr_.size(); }
    bool empty() const noexcept { return field_descr_.empty(); }

    // Element access (all materialize a field_description by value)
    reference operator[](size_type i) const { return as_view()[i]; }
    // TODO: at()
    reference front() const { return as_view().front(); }
    reference back() const { return as_view().back(); }
};

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

// An owning, random-access range over the rows of a resultset (owning version of rows_view).
// Values are stored flat (row-major); each element is a row_view materialized on access.
class rows
{
    std::vector<detail::offset_and_length> values_;
    std::size_t num_columns_{};
    std::vector<unsigned char> data_;

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
    using value_type = row_view;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using reference = row_view;
    using const_reference = row_view;
    using iterator = rows_view::iterator;
    using const_iterator = iterator;

    rows() = default;

    // The elements and their referenced data are owned by us, so the view's
    // pointers stay valid after the temporary view is gone.
    rows_view as_view() const noexcept { return {values_, num_columns_, data_.data()}; }

    operator rows_view() const noexcept { return as_view(); }

    // Removes all data, allowing for memory re-use
    void clear() { set_num_columns(0u); }

    // Part of the unstable API. Should only be used by
    // response authors.
    void set_num_columns(std::size_t num_columns) noexcept
    {
        values_.clear();
        data_.clear();
        num_columns_ = num_columns;
    }

    // Part of the unstable API. Should only be used by
    // response authors.
    void add_row(const protocol::data_row& row)
    {
        // The number of columns must have been set beforehand.
        BOOST_ASSERT(row.columns.size() == num_columns_);
        values_.reserve(values_.size() + row.columns.size());
        for (const auto fv : row.columns)
        {
            if (fv.is_null())
                values_.push_back({.offset = 0u, .length = static_cast<std::size_t>(-1)});
            else
                values_.push_back(insert_data(fv.data()));
        }
    }

    // Iterators
    iterator begin() const noexcept { return as_view().begin(); }
    iterator end() const noexcept { return as_view().end(); }

    // Capacity
    size_type size() const noexcept { return as_view().size(); }
    bool empty() const noexcept { return as_view().empty(); }
    size_type num_columns() const noexcept { return num_columns_; }

    // Element access (all materialize a row_view by value)
    reference operator[](size_type i) const noexcept { return as_view()[i]; }
    // TODO: at()
    reference front() const noexcept { return as_view().front(); }
    reference back() const noexcept { return as_view().back(); }
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

    // Clears the resultset, allowing for memory re-use
    void clear()
    {
        field_descr_.clear();
        values_.clear();
        data_.clear();
        command_complete_tag_ = {};
        portal_suspended_ = false;
    }

    // Part of the unstable API. Should only be used by
    // response authors.
    void set_row_description(const protocol::row_description& row_descr)
    {
        clear();
        field_descr_.reserve(row_descr.field_descriptions.size());
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

    // Part of the unstable API. Should only be used by
    // response authors.
    void add_row(const protocol::data_row& row)
    {
        // Ensuring that this precondition meets is not this class' responsibility,
        // but the response type's.
        BOOST_ASSERT(row.columns.size() == field_descr_.size());
        for (const auto fv : row.columns)
        {
            if (fv.is_null())
                values_.push_back({.offset = 0u, .length = static_cast<std::size_t>(-1)});
            else
                values_.push_back(insert_data(fv.data()));
        }
    }

    // Part of the unstable API. Should only be used by
    // response authors.
    void set_command_complete_tag(std::string_view tag) { command_complete_tag_ = insert_data(tag); }

    // Part of the unstable API. Should only be used by
    // response authors.
    void set_portal_suspended(bool value) { portal_suspended_ = value; }

    // Retrieves the field descriptions (one per column)
    field_descriptions_view field_descriptions() const noexcept { return {field_descr_, data_.data()}; }

    // Retrieves the rows
    rows_view rows() const noexcept { return {values_, field_descr_.size(), data_.data()}; }

    // This is equivalent to PQcmdStatus
    // TODO: this tag is what libpq uses for PQcmdTuples and PQoidValue,
    // implement functions to parse it
    std::string_view command_complete_tag() const noexcept
    {
        return command_complete_tag_.to_string_view(data_.data());
    }

    // True if the query finished with portal suspended, meaning that
    // the portal can be executed again for more rows
    bool portal_suspended() const noexcept { return portal_suspended_; }
};

namespace detail {

struct resultset_descriptor
{
    // TODO: variant?
    extended_error err;
    command_info info;
    offset_and_length descr;
    offset_and_length values;
};

}  // namespace detail

// A view into a single resultset. The element type of resultsets
class resultset_view
{
    const detail::offsetted_field_description* descr_{};
    const detail::offset_and_length* values_{};
    const unsigned char* data_{};
    const detail::resultset_descriptor* result_{};

public:
    resultset_view() = default;
    // TODO: hide
    resultset_view(
        const detail::offsetted_field_description* descr,
        const detail::offset_and_length* values,
        const unsigned char* data,
        const detail::resultset_descriptor* result
    ) noexcept
        : descr_(descr), values_(values), data_(data), result_(result)
    {
    }

    field_descriptions_view field_descriptions() const
    {
        return {
            {descr_ + result_->descr.offset, result_->descr.length},
            data_
        };
    }

    rows_view rows() const
    {
        return {
            {values_ + result_->values.offset, result_->values.length},
            result_->descr.length,
            data_
        };
    }

    const command_info& info() const { return result_->info; }

    const extended_error& error() const { return result_->err; }
};

// A sequence of resultsets. Can accommodate the response to any number of SQL commands
class resultsets
{
    std::vector<detail::offsetted_field_description> field_descr_;
    std::vector<detail::offset_and_length> values_;
    std::vector<detail::resultset_descriptor> resultsets_;
    std::vector<unsigned char> data_;

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
    void add_row_description(const protocol::row_description& row_descr)
    {
        field_descr_.reserve(field_descr_.size() + row_descr.field_descriptions.size());
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
                values_.push_back(insert_data(fv.data()));
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
