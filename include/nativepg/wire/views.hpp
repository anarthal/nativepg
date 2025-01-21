//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_WIRE_VIEWS_HPP
#define NATIVEPG_WIRE_VIEWS_HPP

// Views that deserialize fields while iterating, avoiding copies.
// Not to be instantiated by the user with arbitrary types.
// Implemented in terms of internal traits that the library specializes for the types that might appear in
// messages. Avoids code duplication

#include <boost/core/span.hpp>

#include <cstddef>

namespace nativepg {
namespace protocol {

namespace detail {

template <class T>
struct forward_traits;

template <class T>
struct random_access_collection_traits;

}  // namespace detail

// View over a serialized collections of items of variable size
template <class T>
class forward_parsing_view
{
    std::size_t size_;                       // number of items
    boost::span<const unsigned char> data_;  // serialized items

public:
    class iterator
    {
        const unsigned char* data_;  // pointer into the serialized collection

        iterator(const unsigned char* data) noexcept : data_(data) {}
        T dereference() const { return detail::forward_traits<T>::dereference(data_); }
        void advance() { data_ = detail::forward_traits<T>::advance(data_); }

        friend class columns_view;

    public:
        using value_type = T;
        using reference = T;
        using pointer = T;
        using difference_type = std::ptrdiff_t;
        using iterator_category = std::forward_iterator_tag;

        iterator& operator++() noexcept
        {
            advance();
            return *this;
        }
        iterator operator++(int) noexcept
        {
            auto res = *this;
            advance();
            return res;
        }
        reference operator*() const noexcept { return dereference(); }
        bool operator==(iterator rhs) const noexcept { return data_ == rhs.data_; }
        bool operator!=(iterator rhs) const noexcept { return !(*this == rhs); }
    };

    using const_iterator = iterator;
    using value_type = T;
    using reference = value_type;
    using const_reference = value_type;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;

    // of the DataRow message, and must be valid. parse() performs this validation.
    // Don't use this unless you know what you're doing (TODO: make this private?)
    forward_parsing_view(std::size_t size, boost::span<const unsigned char> data) noexcept
        : size_(size), data_(data)
    {
    }

    // The number of fields
    std::size_t size() const { return size_; }

    // Is the range empty?
    bool empty() const { return size_ == 0u; }

    // Range functions
    iterator begin() const { return iterator(data_.begin()); }
    iterator end() const { return iterator(data_.end()); }
};

}  // namespace protocol
}  // namespace nativepg

#endif
