//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_NOTIFICATION_VECTOR_HPP
#define NATIVEPG_NOTIFICATION_VECTOR_HPP

#include <boost/assert.hpp>
#include <boost/throw_exception.hpp>

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#include "nativepg/protocol/async.hpp"

namespace nativepg {

// A container of notifications that owns the strings they point to.
// Strings live in a single block, so re-using a vector amortizes to no allocations
// TODO: unit test
// TODO: move to cpp
class notification_vector
{
public:
    using value_type = protocol::notification_response;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using reference = const value_type&;
    using const_reference = const value_type&;
    using pointer = const value_type*;
    using const_pointer = const value_type*;

    // Elements are never modified in place, so both iterator flavours are const.
    // Guaranteed to be contiguous iterators; don't rely on them being pointers
    using iterator = const value_type*;
    using const_iterator = const value_type*;
    using reverse_iterator = std::reverse_iterator<const_iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

    notification_vector() = default;

    // Moving is cheap: the strings keep their address, so the views don't need rebasing.
    // The moved-from vector is left empty
    notification_vector(notification_vector&& other) noexcept
        : elms_(std::move(other.elms_)),
          data_{
              std::move(other.data_.data),
              std::exchange(other.data_.size, 0u),
              std::exchange(other.data_.capacity, 0u)
          }
    {
    }

    notification_vector& operator=(notification_vector&& other) noexcept
    {
        if (this != &other)
        {
            elms_ = std::move(other.elms_);
            data_.data = std::move(other.data_.data);
            data_.size = std::exchange(other.data_.size, 0u);
            data_.capacity = std::exchange(other.data_.capacity, 0u);

            // Unlike the move constructor, move-assigning a vector
            // doesn't guarantee that the source is left empty
            other.elms_.clear();
        }
        return *this;
    }

    // TODO: these should really not be = delete
    notification_vector(const notification_vector&) = delete;
    notification_vector& operator=(const notification_vector&) = delete;

    void push_back(const protocol::notification_response& notif)
    {
        // Make room for both strings in one go, so that appending the second one
        // can't reallocate and leave the first one dangling
        grow(data_.size + notif.channel_name.size() + notif.payload.size());

        const auto channel_name = append(notif.channel_name);
        const auto payload = append(notif.payload);
        elms_.push_back({notif.process_id, channel_name, payload});
    }

    void clear()
    {
        // Capacity is retained, so re-using the vector doesn't allocate
        elms_.clear();
        data_.size = 0u;
    }

    // Iterators
    const_iterator begin() const noexcept { return elms_.data(); }
    const_iterator end() const noexcept { return elms_.data() + elms_.size(); }
    const_iterator cbegin() const noexcept { return begin(); }
    const_iterator cend() const noexcept { return end(); }
    const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator{end()}; }
    const_reverse_iterator rend() const noexcept { return const_reverse_iterator{begin()}; }
    const_reverse_iterator crbegin() const noexcept { return rbegin(); }
    const_reverse_iterator crend() const noexcept { return rend(); }

    // Capacity
    size_type size() const noexcept { return elms_.size(); }
    bool empty() const noexcept { return elms_.empty(); }

    // Element access
    const_reference operator[](size_type i) const noexcept
    {
        BOOST_ASSERT(i < size());
        return elms_[i];
    }

    const_reference at(size_type i) const
    {
        if (i >= size())
            BOOST_THROW_EXCEPTION(std::out_of_range("notification_vector::at"));
        return elms_[i];
    }

    const_reference front() const noexcept
    {
        BOOST_ASSERT(!empty());
        return elms_.front();
    }

    const_reference back() const noexcept
    {
        BOOST_ASSERT(!empty());
        return elms_.back();
    }

    const_pointer data() const noexcept { return elms_.data(); }

private:
    // Owns the strings that the views in elms_ point to
    struct flat_buffer
    {
        std::unique_ptr<char[]> data;
        std::size_t size{};
        std::size_t capacity{};
    };

    std::vector<protocol::notification_response> elms_;
    flat_buffer data_;

    // Powers of 2, starting at 512, to prevent many small allocations
    // TODO: this could use C++20 bit ops, and can overflow
    static inline std::size_t compute_capacity(std::size_t current, std::size_t requested)
    {
        std::size_t res = (std::max)(current, static_cast<std::size_t>(512u));
        while (res < requested)
            res *= 2u;
        return res;
    }

    static inline void rebase_string(std::string_view& value, const char* old_base, const char* new_base)
    {
        if (!value.empty())
        {
            const auto offset = value.data() - old_base;
            BOOST_ASSERT(offset >= 0);
            value = {new_base + offset, value.size()};
        }
    }

    // Updates the stored views by performing pointer arithmetic
    void rebase_strings(const char* old_base, const char* new_base)
    {
        for (auto& elm : elms_)
        {
            rebase_string(elm.channel_name, old_base, new_base);
            rebase_string(elm.payload, old_base, new_base);
        }
    }

    // Grows the buffer until reaching a target capacity. Might rebase the stored views
    void grow(std::size_t new_capacity)
    {
        if (new_capacity <= data_.capacity)
            return;

        // Compute the actual capacity that we will be using
        new_capacity = compute_capacity(data_.capacity, new_capacity);

        // Allocate space and copy over whatever we had
        std::unique_ptr<char[]> new_buffer{new char[new_capacity]};
        const char* data_before = data_.data.get();
        char* data_after = new_buffer.get();
        std::copy(data_before, data_before + data_.size, data_after);

        // Update the views so they don't dangle
        rebase_strings(data_before, data_after);

        // Replace the buffer. Note that size hasn't changed here
        data_.data = std::move(new_buffer);
        data_.capacity = new_capacity;
    }

    // Appends a string to the buffer, returning a view to the copy.
    // Might rebase the stored views, but doesn't add any new element
    std::string_view append(std::string_view value)
    {
        // If there is nothing to copy, don't retain a pointer to the caller's buffer
        if (value.empty())
            return {};

        // Make space for the new string
        const std::size_t new_size = data_.size + value.size();
        grow(new_size);

        // Copy the new value
        const std::size_t offset = data_.size;
        std::copy(value.data(), value.data() + value.size(), data_.data.get() + offset);
        data_.size = new_size;
        return {data_.data.get() + offset, value.size()};
    }
};

}  // namespace nativepg

#endif
