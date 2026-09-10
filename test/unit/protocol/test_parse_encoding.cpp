//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/core/lightweight_test.hpp>

#include <optional>

#include "nativepg/encoding.hpp"
#include "nativepg/protocol/parse_encoding.hpp"
#include "test_utils/printing.hpp"
#include "test_utils/test_opt_eq.hpp"

using namespace nativepg;
using namespace nativepg::test;
using protocol::parse_encoding;

namespace {

// Every canonical name that the server may report is recognized
void test_valid()
{
    test_opt_eq(parse_encoding("BIG5"), encoding::big5);
    test_opt_eq(parse_encoding("EUC_CN"), encoding::euc_cn);
    test_opt_eq(parse_encoding("EUC_JP"), encoding::euc_jp);
    test_opt_eq(parse_encoding("EUC_JIS_2004"), encoding::euc_jis_2004);
    test_opt_eq(parse_encoding("EUC_KR"), encoding::euc_kr);
    test_opt_eq(parse_encoding("EUC_TW"), encoding::euc_tw);
    test_opt_eq(parse_encoding("GB18030"), encoding::gb18030);
    test_opt_eq(parse_encoding("GBK"), encoding::gbk);
    test_opt_eq(parse_encoding("ISO_8859_5"), encoding::iso_8859_5);
    test_opt_eq(parse_encoding("ISO_8859_6"), encoding::iso_8859_6);
    test_opt_eq(parse_encoding("ISO_8859_7"), encoding::iso_8859_7);
    test_opt_eq(parse_encoding("ISO_8859_8"), encoding::iso_8859_8);
    test_opt_eq(parse_encoding("JOHAB"), encoding::johab);
    test_opt_eq(parse_encoding("KOI8R"), encoding::koi8r);
    test_opt_eq(parse_encoding("KOI8U"), encoding::koi8u);
    test_opt_eq(parse_encoding("LATIN1"), encoding::latin1);
    test_opt_eq(parse_encoding("LATIN2"), encoding::latin2);
    test_opt_eq(parse_encoding("LATIN3"), encoding::latin3);
    test_opt_eq(parse_encoding("LATIN4"), encoding::latin4);
    test_opt_eq(parse_encoding("LATIN5"), encoding::latin5);
    test_opt_eq(parse_encoding("LATIN6"), encoding::latin6);
    test_opt_eq(parse_encoding("LATIN7"), encoding::latin7);
    test_opt_eq(parse_encoding("LATIN8"), encoding::latin8);
    test_opt_eq(parse_encoding("LATIN9"), encoding::latin9);
    test_opt_eq(parse_encoding("LATIN10"), encoding::latin10);
    test_opt_eq(parse_encoding("MULE_INTERNAL"), encoding::mule_internal);
    test_opt_eq(parse_encoding("SJIS"), encoding::sjis);
    test_opt_eq(parse_encoding("SHIFT_JIS_2004"), encoding::shift_jis_2004);
    test_opt_eq(parse_encoding("SQL_ASCII"), encoding::sql_ascii);
    test_opt_eq(parse_encoding("UHC"), encoding::uhc);
    test_opt_eq(parse_encoding("UTF8"), encoding::utf8);
    test_opt_eq(parse_encoding("UNICODE"), encoding::utf8);
    test_opt_eq(parse_encoding("WIN866"), encoding::win866);
    test_opt_eq(parse_encoding("WIN874"), encoding::win874);
    test_opt_eq(parse_encoding("WIN1250"), encoding::win1250);
    test_opt_eq(parse_encoding("WIN1251"), encoding::win1251);
    test_opt_eq(parse_encoding("WIN1252"), encoding::win1252);
    test_opt_eq(parse_encoding("WIN1253"), encoding::win1253);
    test_opt_eq(parse_encoding("WIN1254"), encoding::win1254);
    test_opt_eq(parse_encoding("WIN1255"), encoding::win1255);
    test_opt_eq(parse_encoding("WIN1256"), encoding::win1256);
    test_opt_eq(parse_encoding("WIN1257"), encoding::win1257);
    test_opt_eq(parse_encoding("WIN1258"), encoding::win1258);
}

void test_invalid()
{
    // Names we don't know about are rejected
    test_opt_eq(parse_encoding("NONSENSE"), std::nullopt);

    // The server always reports canonical, uppercase names, so lowercase is an error
    test_opt_eq(parse_encoding("utf8"), std::nullopt);

    // The empty string is not a valid name
    test_opt_eq(parse_encoding(""), std::nullopt);
}

}  // namespace

int main()
{
    test_valid();
    test_invalid();

    return boost::report_errors();
}
