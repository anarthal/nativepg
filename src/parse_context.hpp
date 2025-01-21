//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_SRC_PARSE_CONTEXT_HPP
#define NATIVEPG_SRC_PARSE_CONTEXT_HPP

#include <boost/assert.hpp>
#include <boost/core/span.hpp>
#include <boost/endian/detail/endian_load.hpp>
#include <boost/system/detail/error_code.hpp>
#include <boost/system/error_code.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <string_view>
#include <type_traits>

#include "nativepg/client_errc.hpp"

namespace nativepg {
namespace protocol {
namespace detail {

// Unchecked versions, for views
template <class IntType>
IntType unchecked_get_integral(const unsigned char*& it)
{
    auto res = boost::endian::endian_load<IntType, sizeof(IntType), boost::endian::order::big>(it);
    it += sizeof(IntType);
    return res;
}

inline std::string_view unchecked_get_string(const unsigned char*& it)
{
    const char* data = reinterpret_cast<const char*>(it);
    auto len = std::strlen(data);
    std::string_view res{data, len};
    it += len + 1u;  // skip NULL terminator, too
    return res;
}

class parse_context
{
    const unsigned char* first_;
    const unsigned char* last_;
    boost::system::error_code ec_;

public:
    parse_context(boost::span<const unsigned char> range) noexcept : first_(range.begin()), last_(range.end())
    {
    }

    const unsigned char* first() const { return first_; }
    const unsigned char* last() const { return last_; }

    std::size_t size() const { return last_ - first_; }

    void advance(std::size_t by)
    {
        BOOST_ASSERT(by <= size());
        first_ += by;
    }

    unsigned char get_byte()
    {
        if (size() < 1)
            add_error(client_errc::incomplete_message);
        return ec_ ? 0u : *first_++;
    }

    template <class IntType>
    IntType get_integral()
    {
        static_assert(std::is_signed<IntType>::value, "Postgres only uses signed types");
        if (size() < sizeof(IntType))
            add_error(client_errc::incomplete_message);
        if (ec_)
            return {};
        return unchecked_get_integral<IntType>(first_);
    }

    template <class IntType>
    IntType get_nonnegative_integral()
    {
        auto res = get_integral<IntType>();
        if (res < 0)
        {
            add_error(client_errc::protocol_value_error);
            return {};
        }
        return res;
    }

    std::string_view get_string()
    {
        if (ec_)
            return {};

        // Search for the NULL terminator
        auto null_it = std::find(first_, last_, static_cast<unsigned char>(0));
        if (null_it == last_)
        {
            add_error(client_errc::incomplete_message);
            return {};
        }

        std::string_view res{reinterpret_cast<const char*>(first_), reinterpret_cast<const char*>(null_it)};

        // Advance, skipping the NULL-terminator
        advance(res.size() + 1u);

        return res;
    }

    void check_size_and_advance(std::size_t n)
    {
        if (n > size())
            add_error(client_errc::incomplete_message);
        else
            advance(n);
    }

    boost::span<const unsigned char> get_bytes(std::size_t n)
    {
        if (n > size())
            add_error(client_errc::incomplete_message);
        if (ec_)
            return {};
        boost::span<const unsigned char> res{first_, n};
        advance(n);
        return res;
    }

    template <std::size_t N>
    std::array<unsigned char, N> get_byte_array()
    {
        std::array<unsigned char, N> res{};
        if (N > size())
            add_error(client_errc::incomplete_message);
        if (ec_)
            return res;
        std::memcpy(res.data(), first_, N);
        advance(N);
        return res;
    }

    void add_error(boost::system::error_code ec)
    {
        if (!ec_)
            ec_ = ec;
    }

    void check_extra_bytes()
    {
        if (first_ != last_)
            add_error(client_errc::extra_bytes);
    }

    boost::system::error_code error() const { return ec_; }

    boost::system::error_code check()
    {
        check_extra_bytes();
        return error();
    }
};

}  // namespace detail
}  // namespace protocol
}  // namespace nativepg

#endif
