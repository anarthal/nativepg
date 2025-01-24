//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_PROTOCOL_BIND_HPP
#define NATIVEPG_PROTOCOL_BIND_HPP

#include <boost/compat/function_ref.hpp>
#include <boost/core/span.hpp>
#include <boost/system/error_code.hpp>
#include <boost/variant2/variant.hpp>

#include <cstddef>
#include <string_view>
#include <vector>

#include "nativepg/protocol/common.hpp"

namespace nativepg {
namespace protocol {
namespace detail {
struct bind_context_access;
}

class bind_context
{
    static inline constexpr std::size_t no_offset = static_cast<std::size_t>(-1);

    std::size_t num_params_{};
    std::size_t param_offset_{no_offset};
    std::vector<unsigned char>& buff_;
    boost::system::error_code err_;

    friend struct detail::bind_context_access;

    void maybe_finish_parameter();

public:
    // Constructor - usually called by the library
    bind_context(std::vector<unsigned char>& buff) noexcept : buff_(buff) {}

    // Starts a parameter. Add its value with one or several add_parameter_chunk calls
    void start_parameter()
    {
        // If this is not the first parameter, write the previous one
        maybe_finish_parameter();

        // Record that we're starting a parameter
        ++num_params_;
        param_offset_ = buff_.size();

        // Allocate space for the parameter size
        buff_.resize(buff_.size() + 4u);
    }

    // Adds a chunk to a parameter
    void add_parameter_chunk(boost::span<const unsigned char> data)
    {
        BOOST_ASSERT(param_offset_ != no_offset);
        buff_.insert(buff_.end(), data.begin(), data.end());
    }

    // Marks the serialization as failed. Only the first error is retained
    void add_error(boost::system::error_code err)
    {
        if (!err_)
            err_ = err;
    }

    // Returns any error added during serialization. Only the 1st one is retained
    boost::system::error_code error() const { return err_; }
};

struct bind
{
    // TODO: should we have a generator option here, too?
    // TODO: maybe this should be a standalone type?
    // If a format_code, sends only this format code (does this even work for binary?)
    // Otherwise, sends an array of format codes
    using format_codes = boost::variant2::variant<format_code, boost::span<const format_code>>;

    // The name of the destination portal (an empty string selects the unnamed portal).
    std::string_view portal_name;

    // The name of the source prepared statement (an empty string selects the unnamed prepared statement).
    std::string_view statement_name;

    // The parameter format codes
    format_codes parameter_fmt_codes;

    // The actual parameters. The number of parameters must match the number of parameters required by the
    // query. The passed function will be called once by the implementation - it should use bind_context
    // to serialize the parameters
    boost::compat::function_ref<void(bind_context&)> parameters_fn;

    // The result-column format codes.
    format_codes result_fmt_codes;
};
boost::system::error_code serialize(const bind& msg, std::vector<unsigned char>& to);

struct bind_complete
{
};
inline boost::system::error_code parse(boost::span<const unsigned char> data, bind_complete&)
{
    return detail::check_empty(data);
}

}  // namespace protocol
}  // namespace nativepg

#endif
