//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <optional>
#include <string_view>

#include "nativepg/protocol/parse_encoding.hpp"

using namespace nativepg;

namespace {

struct encoding_entry
{
    std::string_view name;
    encoding value;
};

// The server always reports the uppercase name, even if
// the client used lowercase when requesting the encoding.
// UNICODE is a synonym for UTF-8
// See https://www.postgresql.org/docs/current/multibyte.html
constexpr encoding_entry encoding_entries[] = {
    {"BIG5",           encoding::big5          },
    {"EUC_CN",         encoding::euc_cn        },
    {"EUC_JP",         encoding::euc_jp        },
    {"EUC_JIS_2004",   encoding::euc_jis_2004  },
    {"EUC_KR",         encoding::euc_kr        },
    {"EUC_TW",         encoding::euc_tw        },
    {"GB18030",        encoding::gb18030       },
    {"GBK",            encoding::gbk           },
    {"ISO_8859_5",     encoding::iso_8859_5    },
    {"ISO_8859_6",     encoding::iso_8859_6    },
    {"ISO_8859_7",     encoding::iso_8859_7    },
    {"ISO_8859_8",     encoding::iso_8859_8    },
    {"JOHAB",          encoding::johab         },
    {"KOI8R",          encoding::koi8r         },
    {"KOI8U",          encoding::koi8u         },
    {"LATIN1",         encoding::latin1        },
    {"LATIN2",         encoding::latin2        },
    {"LATIN3",         encoding::latin3        },
    {"LATIN4",         encoding::latin4        },
    {"LATIN5",         encoding::latin5        },
    {"LATIN6",         encoding::latin6        },
    {"LATIN7",         encoding::latin7        },
    {"LATIN8",         encoding::latin8        },
    {"LATIN9",         encoding::latin9        },
    {"LATIN10",        encoding::latin10       },
    {"MULE_INTERNAL",  encoding::mule_internal },
    {"SJIS",           encoding::sjis          },
    {"SHIFT_JIS_2004", encoding::shift_jis_2004},
    {"SQL_ASCII",      encoding::sql_ascii     },
    {"UHC",            encoding::uhc           },
    {"UTF8",           encoding::utf8          },
    {"UNICODE",        encoding::utf8          },
    {"WIN866",         encoding::win866        },
    {"WIN874",         encoding::win874        },
    {"WIN1250",        encoding::win1250       },
    {"WIN1251",        encoding::win1251       },
    {"WIN1252",        encoding::win1252       },
    {"WIN1253",        encoding::win1253       },
    {"WIN1254",        encoding::win1254       },
    {"WIN1255",        encoding::win1255       },
    {"WIN1256",        encoding::win1256       },
    {"WIN1257",        encoding::win1257       },
    {"WIN1258",        encoding::win1258       },
};

}  // namespace

std::optional<encoding> nativepg::protocol::parse_encoding(std::string_view name)
{
    // The table is small and this only runs on ParameterStatus messages,
    // so a linear scan is fine.
    for (const auto& entry : encoding_entries)
    {
        if (entry.name == name)
            return entry.value;
    }
    return {};
}
