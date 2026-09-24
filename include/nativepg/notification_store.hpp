//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_NOTIFICATION_STORE_HPP
#define NATIVEPG_NOTIFICATION_STORE_HPP

#include <boost/assert.hpp>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "nativepg/protocol/async.hpp"

namespace nativepg {

// Two modes, deep (full copy) and shallow (shallow copies)
// TODO: unit test
// TODO: move to cpp
class notification_store
{
public:
    // Constructed as deep by default
    notification_store() = default;

    // The moved-from store is left empty, retaining its mode
    notification_store(notification_store&& other) noexcept
        : elms_(std::move(other.elms_)),
          data_{
              std::move(other.data_.data),
              std::exchange(other.data_.size, 0u),
              std::exchange(other.data_.capacity, 0u)
          },
          deep_(other.deep_)
    {
    }

    notification_store& operator=(notification_store&& other) noexcept
    {
        if (this != &other)
        {
            elms_ = std::move(other.elms_);
            data_.data = std::move(other.data_.data);
            data_.size = std::exchange(other.data_.size, 0u);
            data_.capacity = std::exchange(other.data_.capacity, 0u);
            deep_ = other.deep_;

            // Unlike the move constructor, move-assigning a vector
            // doesn't guarantee that the source is left empty
            other.elms_.clear();
        }
        return *this;
    }

    notification_store(const notification_store&) = delete;
    notification_store& operator=(const notification_store&) = delete;

    bool is_deep() const { return deep_; }

    // Precondition: container empty
    void set_deep(bool deep)
    {
        BOOST_ASSERT(elms_.empty());
        deep_ = deep;
        data_.size = 0u;
    }

    void push_back(const protocol::notification_response& notif)
    {
        // In shallow mode, the caller owns the strings and we just retain the views
        if (!deep_)
        {
            elms_.push_back(notif);
            return;
        }

        // Make room for both strings in one go, so that appending the second one
        // can't reallocate and leave the first one dangling
        grow(data_.size + notif.channel_name.size() + notif.payload.size());

        const auto channel_name = append(notif.channel_name);
        const auto payload = append(notif.payload);
        elms_.push_back({notif.process_id, channel_name, payload});
    }

    void clear()
    {
        // Capacity is retained, so re-using the store doesn't allocate
        elms_.clear();
        data_.size = 0u;
    }

    std::span<const protocol::notification_response> get() const { return elms_; }

private:
    // Owns the strings that the views in elms_ point to, when in deep mode.
    // A single block, so re-using a store amortizes to no allocations
    struct flat_buffer
    {
        std::unique_ptr<char[]> data;
        std::size_t size{};
        std::size_t capacity{};
    };

    std::vector<protocol::notification_response> elms_;
    flat_buffer data_;
    bool deep_{true};

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
