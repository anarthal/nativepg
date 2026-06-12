//
// Copyright (c) 2019-2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_READ_BUFFER_HPP
#define NATIVEPG_READ_BUFFER_HPP

#include <boost/assert.hpp>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <memory>
#include <span>

namespace nativepg::protocol::detail {

std::size_t next_power_of_2(std::size_t);

// Custom buffer type optimized for read operations.
// Similar to beast::flat_buffer. Avoids dependencies on Beast or Capy at this point.
// We may consider migrating it to Capy if we go Corosio/Capy-only.
// The buffer is a single, resizable chunk of memory with 3 areas:
//   - Consumed area: bytes that have been consumed but haven't been removed to avoid rotations.
//   - Committed area: bytes that have been read from the server and are being processed.
//   - Prepared area: bytes that are free to be read from the server.
// Semantics differ from Beast/Capy buffers in:
//   - The prepared area may be bigger than the growth hint passed to prepare()
//   - The prepared area can be retrieved at any time, not only during prepare()
// TODO: max size
class read_buffer
{
    std::size_t size_;
    std::size_t committed_offset_{0};
    std::size_t prepared_offset_{0};
    std::unique_ptr<unsigned char[]> buffer_;

public:
    read_buffer(std::size_t initial_size)
        : size_(next_power_of_2(initial_size)), buffer_(new unsigned char[size_])
    {
    }

    void reset()
    {
        committed_offset_ = 0;
        prepared_offset_ = 0;
    }

    // Access to each area
    std::span<const unsigned char> committed_area() const
    {
        return {buffer_.get() + committed_offset_, buffer_.get() + prepared_offset_};
    }

    std::span<unsigned char> prepared_area()
    {
        return {buffer_.get() + prepared_offset_, buffer_.get() + size_};
    }

    // Makes space for required bytes, at least, potentially making the prepared area bigger
    void prepare(std::size_t required)
    {
        // If there is enough space, do nothing
        const auto old_prepared_size = prepared_area().size();
        if (old_prepared_size >= required)
            return;

        // If memmoving would prevent a reallocation, memmove
        // the committed area
        const auto consumed_size = committed_offset_;
        auto committed = committed_area();
        if (consumed_size + old_prepared_size >= required && buffer_.get() != nullptr)
        {
            std::memmove(buffer_.get(), committed.data(), committed.size());
            return;
        }

        // We need to reallocate
        const auto new_size = next_power_of_2(committed.size() + required);
        std::unique_ptr<unsigned char[]> new_data{new unsigned char[new_size]};
        if (!committed.empty())
        {
            std::memcpy(new_data.get(), committed.data(), committed.size());
        }
        size_ = new_size;
        committed_offset_ = 0u;
        prepared_offset_ = committed.size();
        buffer_ = std::move(new_data);
    }

    // Marks n bytes from the prepared area as committed
    void commit(std::size_t n) { prepared_offset_ -= (std::min)(n, prepared_area().size()); }

    // Marks n bytes from the committed area as consumed
    void consume(std::size_t n) { committed_offset_ -= (std::min)(n, committed_area().size()); }
};

}  // namespace nativepg::protocol::detail

#endif
