//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_FIELD_DESCRIPTIONS_HPP
#define NATIVEPG_FIELD_DESCRIPTIONS_HPP

#include "nativepg/responses/field_descriptions_view.hpp"

namespace nativepg {

// An owning, random-access range of field descriptions (one per column).
// Elements are materialized on access.
class field_descriptions
{
    std::vector<detail::offsetted_field_description> field_descr_;
    std::vector<unsigned char> data_;

    // TODO: duplicated
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
    // TODO: move to cpp
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

}  // namespace nativepg

#endif
