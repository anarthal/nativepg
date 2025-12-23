//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/asio/disposition.hpp>
#include <boost/assert.hpp>
#include <boost/system/error_category.hpp>
#include <boost/system/system_error.hpp>
#include <boost/throw_exception.hpp>

#include <exception>

#include "nativepg/client_errc.hpp"
#include "nativepg/detail/disposition.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/protocol/notice_error.hpp"

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

void diagnostics::assign(const protocol::error_response& err)
{
    msg_ = err.severity.value_or("<Server error with unknown severity>");
    msg_ += ": ";
    msg_ += err.sqlstate.value_or("<unknown SQLSTATE>");
    msg_ += ": ";
    msg_ += err.message.value_or("<unknown error>");
    // TODO: add more info here
}

void boost::asio::disposition_traits<extended_error>::throw_exception(const nativepg::extended_error& d)
{
    boost::throw_exception(boost::system::system_error(d.code, std::string(d.diag.message())));
}

std::exception_ptr boost::asio::disposition_traits<extended_error>::to_exception_ptr(
    const nativepg::extended_error& d
) noexcept
{
    return std::make_exception_ptr(boost::system::system_error(d.code, std::string(d.diag.message())));
}
