//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/compat/function_ref.hpp>

#include <string_view>
#include <system_error>

#include "nativepg/client_errc.hpp"
#include "nativepg/encoding.hpp"
#include "nativepg/escape.hpp"

using namespace nativepg;

namespace {

// The character used to quote identifiers. It's escaped by doubling it
constexpr char quote_char = '"';
constexpr std::string_view quote_str{&quote_char, 1u};

}  // namespace

std::error_code nativepg::escape_identifier_body(
    std::string_view value,
    encoding enc,
    boost::compat::function_ref<void(std::string_view)> fn
)
{
    // We only support UTF-8, for the time being
    if (enc != encoding::utf8)
        return client_errc::unsupported_encoding;

    // Emit the value, doubling any quote character. UTF-8 is ASCII-safe: all the bytes
    // in a multi-byte sequence have the high bit set, so a quote character can never be
    // part of one, and scanning byte by byte is safe here
    const char* chunk_start = value.data();
    const char* end = value.data() + value.size();
    for (const char* it = value.data(); it != end; ++it)
    {
        if (*it == quote_char)
        {
            // Emit everything up to and including the quote, then the quote that escapes it
            fn({chunk_start, it + 1});
            fn(quote_str);
            chunk_start = it + 1;
        }
    }

    // Whatever is left after the last quote
    if (chunk_start != end)
        fn({chunk_start, end});

    return {};
}
