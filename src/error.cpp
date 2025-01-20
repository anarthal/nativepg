//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/system/detail/error_category.hpp>

#include "nativepg/client_errc.hpp"

using namespace nativepg;

namespace {

static const char* error_to_string(client_errc error)
{
    switch (error)
    {
    case client_errc::incomplete_message: return "An incomplete message was received from the server";
    case client_errc::extra_bytes: return "Unexpected extra bytes at the end of a message were received";
    case client_errc::protocol_value_error:
        return "An unexpected value was found in a server-received message";
    default: return "<unknown nativepg client error>";
    }
}

class client_category final : public boost::system::error_category
{
public:
    const char* name() const noexcept final override { return "nativepg.client"; }
    std::string message(int ev) const final override { return error_to_string(static_cast<client_errc>(ev)); }
};

static client_category g_clicat;

}  // namespace

const boost::system::error_category& nativepg::get_client_category() { return g_clicat; }