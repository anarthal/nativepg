//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_EXTENDED_ERROR_HPP
#define NATIVEPG_EXTENDED_ERROR_HPP

#include <boost/system/error_code.hpp>

#include <string>
#include <string_view>

#include "nativepg/protocol/notice_error.hpp"

namespace nativepg {

class diagnostics
{
    std::string msg_;
    // TODO: consider storing offsets and expose the actual fields in the message (e.g. SQLSTATE, severity...)
public:
    diagnostics() noexcept = default;

    diagnostics(const protocol::error_response& msg) { assign(msg); }

    void assign(const protocol::error_response& msg);

    std::string_view message() const { return msg_; }
};

struct extended_error
{
    boost::system::error_code code;
    diagnostics diag;
};

}  // namespace nativepg

#endif
