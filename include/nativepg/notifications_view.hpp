//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_NOTIFICATIONS_VIEW_HPP
#define NATIVEPG_NOTIFICATIONS_VIEW_HPP

#include <span>

#include "nativepg/protocol/async.hpp"
#include "nativepg/responses/detail/dynamic_utils.hpp"  // TODO: move offset_and_length

namespace nativepg {

namespace detail {

struct offsetted_notification
{
    std::int32_t process_id{};
    offset_and_length channel_name{}, payload{};

    protocol::notification_response to_notification_response(const unsigned char* data) const
    {
        return {
            .process_id = process_id,
            .channel_name = channel_name.to_string_view(data),
            .payload = payload.to_string_view(data),
        };
    }
};

}  // namespace detail

// A random-access, span-like view over a range of notifications.
// Elements are materialized on access.
// TODO: consider implementing this and field_descriptions_view either with ranges::transform(),
//   or with a wrapper to reduce duplication
class notifications_view
{
    std::span<const detail::offsetted_notification> elms_;
    const unsigned char* data_{};

public:
    class iterator
    {
        const detail::offsetted_notification* it_{};
        const unsigned char* data_{};

        friend class notifications_view;
        iterator(const detail::offsetted_notification* it, const unsigned char* data) noexcept
            : it_(it), data_(data)
        {
        }

    public:
        using value_type = protocol::notification_response;
        using reference = protocol::notification_response;  // prvalue, materialized on deref
        using pointer = protocol::notification_response;
        using difference_type = std::ptrdiff_t;
        using iterator_category = std::random_access_iterator_tag;

        iterator() = default;

        reference operator*() const { return it_->to_notification_response(data_); }
        reference operator[](difference_type n) const { return it_[n].to_notification_response(data_); }

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

    using value_type = protocol::notification_response;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using reference = protocol::notification_response;
    using const_reference = protocol::notification_response;
    using const_iterator = iterator;

    notifications_view() = default;

    // TODO: hide this
    notifications_view(
        std::span<const detail::offsetted_notification> elms,
        const unsigned char* data
    ) noexcept
        : elms_(elms), data_(data)
    {
    }

    // Iterators
    iterator begin() const noexcept { return {elms_.data(), data_}; }
    iterator end() const noexcept { return {elms_.data() + elms_.size(), data_}; }

    // Capacity
    size_type size() const noexcept { return elms_.size(); }
    bool empty() const noexcept { return elms_.empty(); }

    // Element access (all materialize a notification_response by value)
    reference operator[](size_type i) const { return elms_[i].to_notification_response(data_); }
    // TODO: at()
    reference front() const { return elms_.front().to_notification_response(data_); }
    reference back() const { return elms_.back().to_notification_response(data_); }
};

}  // namespace nativepg

#endif
