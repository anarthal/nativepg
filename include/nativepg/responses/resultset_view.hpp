//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_RESULTSET_VIEW_HPP
#define NATIVEPG_RESULTSET_VIEW_HPP

#include "nativepg/extended_error.hpp"
#include "nativepg/responses/command_info.hpp"
#include "nativepg/responses/detail/dynamic_utils.hpp"
#include "nativepg/responses/field_descriptions_view.hpp"
#include "nativepg/responses/rows_view.hpp"

namespace nativepg {

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

}  // namespace nativepg

#endif
