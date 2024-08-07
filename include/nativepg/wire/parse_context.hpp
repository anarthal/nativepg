
#pragma once

#include <boost/assert.hpp>
#include <boost/core/span.hpp>
#include <boost/endian/detail/endian_load.hpp>
#include <boost/system/detail/error_code.hpp>
#include <boost/system/error_code.hpp>

#include <algorithm>
#include <cstddef>
#include <string_view>

#include "nativepg/client_errc.hpp"

namespace nativepg {
namespace detail {

class parse_context
{
    const unsigned char* first_;
    const unsigned char* last_;
    boost::system::error_code ec_;

    std::size_t size() const { return last_ - first_; }

    void add_error(boost::system::error_code ec)
    {
        if (!ec_)
            ec_ = ec;
    }

    void advance(std::size_t by)
    {
        BOOST_ASSERT(by <= size());
        first_ += by;
    }

public:
    parse_context(boost::span<const unsigned char> range) noexcept : first_(range.begin()), last_(range.end())
    {
    }

    template <class IntType>
    IntType get_integral()
    {
        if (size() < sizeof(IntType))
            add_error(client_errc::incomplete_message);
        if (ec_)
            return {};
        IntType res{};
        boost::endian::endian_load<IntType, sizeof(IntType), boost::endian::order::big>(first_, res);
        advance(sizeof(IntType));
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
};

}  // namespace detail
}  // namespace nativepg
