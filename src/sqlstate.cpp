//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <system_error>

#include "nativepg/sqlstate.hpp"

using namespace nativepg;

namespace {

// Deduplicates an SQLSTATE value. Some conditions
// can be represented with different SQLSTATE values, depending
// on where it happens. This maps these duplicates to their canonical value
constexpr int to_error_condition_value(int value)
{
    switch (value)
    {
        // 01004 is a warning, mapped to the data exception equivalent
        case "01004"_sqlstate: return static_cast<int>(sqlstate_cond::string_data_right_truncation);
        // 39004 is an External Routine Invocation Exception, mapped to the data exception equivalent
        case "39004"_sqlstate: return static_cast<int>(sqlstate_cond::null_value_not_allowed);
        // 38XXX are External Routine Exceptions, mapped to their SQL routine exception equivalents
        case "38002"_sqlstate: return static_cast<int>(sqlstate_cond::modifying_sql_data_not_permitted);
        case "38003"_sqlstate: return static_cast<int>(sqlstate_cond::prohibited_sql_statement_attempted);
        case "38004"_sqlstate: return static_cast<int>(sqlstate_cond::reading_sql_data_not_permitted);
        // Otherwise, don't touch the value
        default: return value;
    }
}

class sqlstate_category final : public std::error_category
{
public:
    const char* name() const noexcept final override { return "nativepg.sqlstate"; }
    std::string message(
        int ev
    ) const final override;  // TODO { return error_to_string(static_cast<client_errc>(ev)); }
    std::error_condition default_error_condition(int val) const noexcept final override
    {
        return {to_error_condition_value(val), *this};
    }
};

static sqlstate_category g_sqlstatecat;

}  // namespace

const std::error_category& nativepg::get_sqlstate_category() { return g_sqlstatecat; }
