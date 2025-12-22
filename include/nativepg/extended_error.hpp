//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_EXTENDED_ERROR_HPP
#define NATIVEPG_EXTENDED_ERROR_HPP

#include <boost/system/detail/error_code.hpp>

#include <cstddef>
#include <string>
#include <string_view>

#include "nativepg/protocol/notice_error.hpp"

namespace nativepg {

class diagnostics
{
    std::string msg_;
    std::size_t sev_offset_{}, sev_size_{};
    // TODO: include all the fields
public:
    diagnostics() noexcept = default;

    diagnostics(const protocol::error_response& msg);

    std::string_view message() const { return msg_; }
    std::string_view severity() const { return message().substr(sev_offset_, sev_size_); }
};

struct extended_error
{
    boost::system::error_code code;
    diagnostics diag;
};

}  // namespace nativepg

#endif
