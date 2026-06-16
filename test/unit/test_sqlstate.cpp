//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/core/lightweight_test.hpp>

#include <string>
#include <string_view>
#include <system_error>

#include "nativepg/sqlstate.hpp"
#include "nativepg/sqlstate_cond.hpp"
#include "nativepg_internal/sqlstate_as_string.hpp"

using namespace nativepg;

namespace {

//
// String literal
//
static_assert("00015"_sqlstate == 69);
static_assert("00000"_sqlstate == 0);
static_assert("ZZZZZ"_sqlstate == 596523235);

//
// Runtime parsing
//
void test_runtime_parse_success()
{
    const auto& cat = get_sqlstate_category();

    // Zero
    BOOST_TEST_EQ(parse_sqlstate("00000"), std::error_code(0, cat));

    // All digits
    BOOST_TEST_EQ(parse_sqlstate("23505"), std::error_code(34361349, cat));

    // Digits and a letter
    BOOST_TEST_EQ(parse_sqlstate("0A000"), std::error_code(2621440, cat));

    // Mixed
    BOOST_TEST_EQ(parse_sqlstate("42P01"), std::error_code(67735553, cat));

    // High letters
    BOOST_TEST_EQ(parse_sqlstate("HV00B"), std::error_code(293339147, cat));
}

// On error, unknown_sqlstate (error_code -1) is returned
void test_runtime_parse_error()
{
    const std::error_code invalid(-1, get_sqlstate_category());

    // empty string
    BOOST_TEST_EQ(parse_sqlstate(""), invalid);

    // string has < 5 chars
    BOOST_TEST_EQ(parse_sqlstate("0000"), invalid);

    // string has > 5 chars
    BOOST_TEST_EQ(parse_sqlstate("000000"), invalid);

    // string has lowercase chars
    BOOST_TEST_EQ(parse_sqlstate("0a000"), invalid);

    // string has punctuation
    BOOST_TEST_EQ(parse_sqlstate("0000-"), invalid);
    BOOST_TEST_EQ(parse_sqlstate("42:01"), invalid);

    // string has space/newline/control
    BOOST_TEST_EQ(parse_sqlstate("00 00"), invalid);
    BOOST_TEST_EQ(parse_sqlstate("00\n00"), invalid);
    BOOST_TEST_EQ(parse_sqlstate("00\t00"), invalid);

    // string has NULL characters
    const char with_null[] = {'2', '3', '\0', '0', '5'};
    BOOST_TEST_EQ(parse_sqlstate(std::string_view(with_null, 5)), invalid);

    // string has UTF-8 surrogates
    BOOST_TEST_EQ(parse_sqlstate(std::string_view("val\xc3\xb1", 5)), invalid);

    // string has 0xff chars
    BOOST_TEST_EQ(parse_sqlstate(std::string_view("0000\xff", 5)), invalid);

    // Coverage: we detect errors in all the characters
    BOOST_TEST_EQ(parse_sqlstate("a0000"), invalid);
    BOOST_TEST_EQ(parse_sqlstate("0a000"), invalid);
    BOOST_TEST_EQ(parse_sqlstate("00a00"), invalid);
    BOOST_TEST_EQ(parse_sqlstate("000a0"), invalid);
    BOOST_TEST_EQ(parse_sqlstate("0000a"), invalid);
}

//
// Raw code to string conversion
//
std::string as_str_helper(int val)
{
    auto res = detail::sqlstate_as_string(val);
    return {res.begin(), res.end()};
}

void test_as_string_success()
{
    // Zero
    BOOST_TEST_EQ(as_str_helper(0), "00000");

    // All digits
    BOOST_TEST_EQ(as_str_helper(34361349), "23505");

    // Digits and a letter
    BOOST_TEST_EQ(as_str_helper(2621440), "0A000");

    // Mixed
    BOOST_TEST_EQ(as_str_helper(67735553), "42P01");

    // High letters
    BOOST_TEST_EQ(as_str_helper(293339147), "HV00B");
}

// All valid codes can be converted forth and back to string
void test_as_string_round_trip()
{
    constexpr std::string_view test_cases[] = {
        "00000", "01000", "0100C", "01008", "01003", "01007", "01006", "01P01", "02000", "02001", "03000",
        "08000", "08003", "08006", "08001", "08004", "08007", "08P01", "09000", "0A000", "0B000", "0F000",
        "0F001", "0L000", "0LP01", "0P000", "0Z000", "0Z002", "10608", "20000", "21000", "22000", "2202E",
        "22021", "22008", "22012", "22005", "2200B", "22022", "22015", "2201E", "22014", "22016", "2201F",
        "2201G", "22018", "22007", "22019", "2200D", "22025", "22P06", "22010", "22023", "22013", "2201B",
        "2201W", "2201X", "2202H", "2202G", "22009", "2200C", "2200G", "22004", "22002", "22003", "2200H",
        "22026", "22001", "22011", "22027", "22024", "2200F", "22P01", "22P02", "22P03", "22P04", "22P05",
        "2200L", "2200M", "2200N", "2200S", "2200T", "22030", "22031", "22032", "22033", "22034", "22035",
        "22036", "22037", "22038", "22039", "2203A", "2203B", "2203C", "2203D", "2203E", "2203F", "2203G",
        "23000", "23001", "23502", "23503", "23505", "23514", "23P01", "24000", "25000", "25001", "25002",
        "25008", "25003", "25004", "25005", "25006", "25007", "25P01", "25P02", "25P03", "25P04", "26000",
        "27000", "28000", "28P01", "2B000", "2BP01", "2D000", "2F000", "2F005", "2F002", "2F003", "2F004",
        "34000", "38000", "38001", "39000", "39001", "39P01", "39P02", "39P03", "3B000", "3B001", "3D000",
        "3F000", "40000", "40002", "40001", "40003", "40P01", "42000", "42601", "42501", "42846", "42803",
        "42P20", "42P19", "42830", "42602", "42622", "42939", "42804", "42P18", "42P21", "42P22", "42809",
        "428C9", "42703", "42883", "42P01", "42P02", "42704", "42701", "42P03", "42P04", "42723", "42P05",
        "42P06", "42P07", "42712", "42710", "42702", "42725", "42P08", "42P09", "42P10", "42611", "42P11",
        "42P12", "42P13", "42P14", "42P15", "42P16", "42P17", "44000", "53000", "53100", "53200", "53300",
        "53400", "54000", "54001", "54011", "54023", "55000", "55006", "55P02", "55P03", "55P04", "57000",
        "57014", "57P01", "57P02", "57P03", "57P04", "57P05", "58000", "58030", "58P01", "58P02", "58P03",
        "F0000", "F0001", "HV000", "HV005", "HV002", "HV010", "HV021", "HV024", "HV007", "HV008", "HV004",
        "HV006", "HV091", "HV00B", "HV00C", "HV00D", "HV090", "HV00A", "HV009", "HV014", "HV001", "HV00P",
        "HV00J", "HV00K", "HV00Q", "HV00R", "HV00L", "HV00M", "HV00N", "P0000", "P0001", "P0002", "P0003",
        "P0004", "XX000", "XX001", "XX002", "01004", "39004", "38002", "38003", "38004", "ZZZZZ",
    };

    for (const auto str : test_cases)
    {
        int int_value = detail::sqlstate_as_int(str);
        auto str_array = detail::sqlstate_as_string(int_value);
        BOOST_TEST_EQ(std::string_view{str_array}, str);
    }
}

//
// Error category
//
void test_sqlstate_cond()
{
    const auto& cat = get_sqlstate_category();

    // A std::error_condition can be implicitly created from a sqlstate_cond value
    {
        std::error_condition cond = sqlstate_cond::unique_violation;
        BOOST_TEST_EQ(cond.value(), static_cast<int>(sqlstate_cond::unique_violation));
        BOOST_TEST(cond.category() == cat);
    }

    // Casting a std::error_condition containing success (00000) to bool returns false
    {
        std::error_condition cond("00000"_sqlstate, cat);
        BOOST_TEST_NOT(cond);
    }

    // Casting a std::error_condition containing another sqlstate to bool returns true
    {
        std::error_condition cond = sqlstate_cond::unique_violation;
        BOOST_TEST(cond);
    }

    // Comparing a sqlstate code to an unrelated sqlstate condition returns false
    BOOST_TEST(parse_sqlstate("23505") != sqlstate_cond::syntax_error);

    // Comparing a sqlstate code to the same sqlstate condition returns true
    BOOST_TEST(parse_sqlstate("23505") == sqlstate_cond::unique_violation);

    // Comparing the conditions deduplicated in deduplicate_sqlstate_value returns true.
    // These raw SQLSTATEs map to a canonical condition via default_error_condition.
    BOOST_TEST(parse_sqlstate("01004") == sqlstate_cond::string_data_right_truncation);
    BOOST_TEST(parse_sqlstate("39004") == sqlstate_cond::null_value_not_allowed);
    BOOST_TEST(parse_sqlstate("38002") == sqlstate_cond::modifying_sql_data_not_permitted);
    BOOST_TEST(parse_sqlstate("38003") == sqlstate_cond::prohibited_sql_statement_attempted);
    BOOST_TEST(parse_sqlstate("38004") == sqlstate_cond::reading_sql_data_not_permitted);

    // Comparing codes with invalid int values to sqlstate_cond::bad_sqlstate succeeds
    BOOST_TEST(std::error_code(-1, cat) == sqlstate_cond::bad_sqlstate);
    BOOST_TEST(std::error_code(-500, cat) == sqlstate_cond::bad_sqlstate);
    BOOST_TEST(std::error_code(0x7fffffff, cat) == sqlstate_cond::bad_sqlstate);
    BOOST_TEST(std::error_code(36, cat) == sqlstate_cond::bad_sqlstate);
    BOOST_TEST(std::error_code(36 << 6, cat) == sqlstate_cond::bad_sqlstate);
    BOOST_TEST(std::error_code(36 << 12, cat) == sqlstate_cond::bad_sqlstate);
    BOOST_TEST(std::error_code(36 << 18, cat) == sqlstate_cond::bad_sqlstate);
    BOOST_TEST(std::error_code(36 << 24, cat) == sqlstate_cond::bad_sqlstate);

    // Comparing other codes to bad_sqlstate returns false
    BOOST_TEST(parse_sqlstate("38004") != sqlstate_cond::bad_sqlstate);
}

}  // namespace

int main()
{
    test_runtime_parse_success();
    test_runtime_parse_error();
    test_as_string_success();
    test_as_string_round_trip();
    test_sqlstate_cond();

    return boost::report_errors();
}