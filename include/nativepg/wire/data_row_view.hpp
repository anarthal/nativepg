

#pragma once

#include <boost/core/span.hpp>
#include <boost/endian/conversion.hpp>
#include <boost/endian/detail/endian_store.hpp>
#include <boost/endian/detail/order.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string_view>
#include <vector>

namespace nativepg {
namespace detail {

class data_row_view
{
    std::size_t size_;
    boost::span<const unsigned char> data_;

public:
    data_row_view(std::size_t size, boost::span<const unsigned char> data) noexcept : size_(size), data_(data)
    {
    }

    std::size_t size() const { return size_; }
    bool empty() const { return size_ == 0u; }

    class iterator
    {
        const unsigned char* data_;

    public:
        using value_type = boost::span<const unsigned char>;
        using reference = boost::span<const unsigned char>;
        using pointer = boost::span<const unsigned char>;
        using difference_type = std::ptrdiff_t;
        using iterator_category = std::forward_iterator_tag;

        iterator(const unsigned char data) noexcept : data_(data) {}

        iterator& operator++() noexcept
        {
            ++row_num_;
            return *this;
        }
        iterator operator++(int) noexcept
        {
            auto res = *this;
            ++(*this);
            return res;
        }
        reference operator*() const noexcept
        {
            auto sz = static_cast<std::size_t>(boost::endian::load_big_s32(data_));
            return (*this)[0];
        }
        bool operator==(iterator rhs) const noexcept { return data_ == rhs.data_; }
        bool operator!=(iterator rhs) const noexcept { return !(*this == rhs); }
    };

    using const_iterator = iterator;
    using value_type = boost::span<const unsigned char>;
    using reference = value_type;
    using const_reference = value_type;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
};

}  // namespace detail
}  // namespace nativepg
