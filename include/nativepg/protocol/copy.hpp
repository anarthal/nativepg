//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_PROTOCOL_COPY_HPP
#define NATIVEPG_PROTOCOL_COPY_HPP

#include <boost/core/span.hpp>
#include <boost/system/error_code.hpp>

#include <vector>

#include "nativepg/protocol/common.hpp"
#include "nativepg/protocol/views.hpp"

namespace nativepg {
namespace protocol {
namespace detail {

// Collections
template <>
struct random_access_traits<format_code>
{
    static format_code dereference(const unsigned char* data);
};

}  // namespace detail

struct copy_data
{
    // Data that forms part of a COPY data stream. Messages sent from the backend will always correspond to
    // single data rows, but messages sent by frontends might divide the data stream arbitrarily.
    boost::span<const unsigned char> data;
};
inline boost::system::error_code parse(boost::span<const unsigned char> data, copy_data& to)
{
    to.data = data;
    return {};
}

struct copy_done
{
};
inline boost::system::error_code parse(boost::span<const unsigned char> data, copy_done&)
{
    return detail::check_empty(data);
}
boost::system::error_code serialize(copy_done, std::vector<unsigned char>& to);

struct copy_fail
{
    // An error message to report as the cause of failure.
    std::string_view error_message;
};
boost::system::error_code serialize(const copy_fail& msg, std::vector<unsigned char>& to);

struct copy_in_response
{
    // Indicates whether the overall COPY format is textual (rows separated by newlines, columns separated by
    // separator characters, etc.) or binary (similar to DataRow format).
    format_code overall_fmt_code;

    // The format codes to be used for each column. If overall_fmt_code is text, all these must be zero.
    random_access_parsing_view<format_code> fmt_codes;
};
boost::system::error_code parse(boost::span<const unsigned char> data, copy_in_response& to);

struct copy_out_response
{
    // Indicates whether the overall COPY format is textual (rows separated by newlines, columns separated by
    // separator characters, etc.) or binary (similar to DataRow format).
    format_code overall_fmt_code;

    // The format codes to be used for each column. If overall_fmt_code is text, all these must be zero.
    random_access_parsing_view<format_code> fmt_codes;
};
boost::system::error_code parse(boost::span<const unsigned char> data, copy_out_response& to);

struct copy_both_response
{
    // Indicates whether the overall COPY format is textual (rows separated by newlines, columns separated by
    // separator characters, etc.) or binary (similar to DataRow format).
    format_code overall_fmt_code;

    // The format codes to be used for each column. If overall_fmt_code is text, all these must be zero.
    random_access_parsing_view<format_code> fmt_codes;
};
boost::system::error_code parse(boost::span<const unsigned char> data, copy_both_response& to);

}  // namespace protocol
}  // namespace nativepg

#endif
