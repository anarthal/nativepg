//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_ENCODING_HPP
#define NATIVEPG_ENCODING_HPP

#include <optional>
#include <string_view>

namespace nativepg {

enum class encoding
{
    big5,
    euc_cn,
    euc_jp,
    euc_jis_2004,
    euc_kr,
    euc_tw,
    gb18030,
    gbk,
    iso_8859_5,
    iso_8859_6,
    iso_8859_7,
    iso_8859_8,
    johab,
    koi8r,
    koi8u,
    latin1,
    latin2,
    latin3,
    latin4,
    latin5,
    latin6,
    latin7,
    latin8,
    latin9,
    latin10,
    mule_internal,
    sjis,
    shift_jis_2004,
    sql_ascii,
    uhc,
    utf8,
    win866,
    win874,
    win1250,
    win1251,
    win1252,
    win1253,
    win1254,
    win1255,
    win1256,
    win1257,
    win1258,
};

// Parses a client_encoding value, as reported by the server in a ParameterStatus
// message (e.g. "UTF8"). The server always uses the canonical, uppercase name.
//   https://www.postgresql.org/docs/current/multibyte.html
// Returns an empty optional if the name is not one of the encodings we know about.
std::optional<encoding> parse_encoding(std::string_view name);

}  // namespace nativepg

#endif
