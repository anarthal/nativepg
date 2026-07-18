//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/core/lightweight_test.hpp>
#include <boost/decimal.hpp>
#include <boost/system/error_code.hpp>

#include <cstdint>
#include <iomanip>
#include <limits>
#include <span>
#include <sstream>
#include <string>

#include "nativepg/detail/field_traits.hpp"
#include "nativepg/protocol/describe.hpp"
#include "nativepg/types/decimal.hpp"
#include "test_utils.hpp"

using namespace nativepg;
namespace bd = boost::decimal;

namespace {

template <typename T>
    requires std::same_as<T, bd::decimal32_t> || std::same_as<T, bd::decimal64_t> ||
             std::same_as<T, bd::decimal128_t>
void test_parse_text_decimal_success(const T& in_val)
{
    // Arrange
    T out_val;
    std::stringstream ss;
    ss << std::setprecision(std::numeric_limits<T>::max_digits10) << in_val;
    const std::string str = ss.str();
    std::span<const unsigned char> data(reinterpret_cast<const unsigned char*>(str.data()), str.size());

    // Act
    auto err = types::parse_text_decimal(data, out_val);

    // Assert
    NATIVEPG_TEST_EQ(err, boost::system::error_code{});
    if (bd::isnan(in_val))
    {
        NATIVEPG_TEST(bd::isnan(out_val));  // NaN != NaN, so compare by predicate
    }
    else
    {
        NATIVEPG_TEST_EQ(out_val, in_val);
    }
}

template <typename T>
    requires std::same_as<T, bd::decimal32_t> || std::same_as<T, bd::decimal64_t> ||
             std::same_as<T, bd::decimal128_t>
void test_parse_binary_decimal_success(std::span<const unsigned char> wire, const T& expected)
{
    // Arrange
    T out_val;

    // Act
    auto err = types::parse_binary_decimal(wire, out_val);

    // Assert
    NATIVEPG_TEST_EQ(err, boost::system::error_code{});
    if (bd::isnan(expected))
    {
        NATIVEPG_TEST(bd::isnan(out_val));  // NaN != NaN, so compare by predicate
    }
    else
    {
        NATIVEPG_TEST_EQ(out_val, expected);
    }
}

// Verifies a text literal (e.g. one with a leading '+' sign, or leading/trailing zeros) parses to the
// expected value. Unlike test_parse_text_decimal_success, the wire text is given verbatim rather than
// derived from streaming `expected`, so it can cover forms the stream operator never produces.
template <typename T>
    requires std::same_as<T, bd::decimal32_t> || std::same_as<T, bd::decimal64_t> ||
             std::same_as<T, bd::decimal128_t>
void test_parse_text_decimal_from_str(const std::string& str, const T& expected)
{
    // Arrange
    T out_val;
    std::span<const unsigned char> data(reinterpret_cast<const unsigned char*>(str.data()), str.size());

    // Act
    auto err = types::parse_text_decimal(data, out_val);

    // Assert
    NATIVEPG_TEST_EQ(err, boost::system::error_code{});
    NATIVEPG_TEST_EQ(out_val, expected);
}

template <typename T>
    requires std::same_as<T, bd::decimal32_t> || std::same_as<T, bd::decimal64_t> ||
             std::same_as<T, bd::decimal128_t>
void test_parse_text_decimal_error(const std::string& str, boost::system::error_code expected)
{
    // Arrange
    T out_val;
    std::span<const unsigned char> data(reinterpret_cast<const unsigned char*>(str.data()), str.size());

    // Act
    auto ec = types::parse_text_decimal(data, out_val);

    // Assert
    NATIVEPG_TEST_EQ(ec, expected);
}

template <typename T>
    requires std::same_as<T, bd::decimal32_t> || std::same_as<T, bd::decimal64_t> ||
             std::same_as<T, bd::decimal128_t>
void test_parse_binary_decimal_error(std::span<const unsigned char> wire, boost::system::error_code expected)
{
    // Arrange
    T out_val;

    // Act
    auto ec = types::parse_binary_decimal(wire, out_val);

    // Assert
    NATIVEPG_TEST_EQ(ec, expected);
}

// Builds a field_description with the given type OID and format code (the rest of the fields are
// irrelevant to type parsing)
protocol::field_description make_field_description(
    std::int32_t type_oid,
    protocol::format_code fmt_code = protocol::format_code::text
)
{
    return {
        .name = "field",
        .table_oid = 0,
        .column_attribute = 0,
        .type_oid = type_oid,
        .type_length = 0,
        .type_modifier = 0,
        .fmt_code = fmt_code,
    };
}

//
// detail::field_is_compatible / detail::field_parse (field_traits_decimal.hpp)
//
void test_field_is_compatible_decimal_success()
{
    NATIVEPG_TEST_EQ(
        detail::field_is_compatible<bd::decimal64_t>::call(make_field_description(detail::decimal_oid)),
        boost::system::error_code{}
    );
}

void test_field_is_compatible_decimal_incompatible_error()
{
    NATIVEPG_TEST_EQ(
        detail::field_is_compatible<bd::decimal64_t>::call(make_field_description(23 /* int4 oid */)),
        boost::system::error_code(client_errc::incompatible_field_type)
    );
}

void test_field_parse_decimal_text_success()
{
    // Arrange
    bd::decimal64_t out_val;
    const std::string str = "1234.5678";
    std::span<const unsigned char> data(reinterpret_cast<const unsigned char*>(str.data()), str.size());
    field_view fv{data};
    const auto desc = make_field_description(detail::decimal_oid, protocol::format_code::text);

    // Act
    auto err = detail::field_parse<bd::decimal64_t>::call(fv, desc, out_val);

    // Assert
    NATIVEPG_TEST_EQ(err, boost::system::error_code{});
    NATIVEPG_TEST_EQ(out_val, bd::decimal64_t{"1234.5678"});
}

void test_field_parse_decimal_binary_success()
{
    // Arrange
    bd::decimal64_t out_val;
    // 1234.5678: ndigits=2, weight=0, sign=0x0000, dscale=4, groups 1234/5678
    static constexpr unsigned char pg_num_1234_5678[] =
        {0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x04, 0xD2, 0x16, 0x2E};
    field_view fv{pg_num_1234_5678};
    const auto desc = make_field_description(detail::decimal_oid, protocol::format_code::binary);

    // Act
    auto err = detail::field_parse<bd::decimal64_t>::call(fv, desc, out_val);

    // Assert
    NATIVEPG_TEST_EQ(err, boost::system::error_code{});
    NATIVEPG_TEST_EQ(out_val, bd::decimal64_t{1234.5678});
}

// Unlike numeric.hpp's arbitrary-precision types, decimal.hpp performs no significant-digit check:
// decimal32_t has ~7 significant digits of precision, so a numeric text value with more digits than
// that does not error out (no client_errc::incompatible_response_length path exists here) — it is
// silently rounded the same way boost::decimal's own string constructor rounds it. This test pins
// that (intentional) behavior so a future change doesn't silently alter it.
void test_parse_text_decimal_precision_loss()
{
    bd::decimal32_t out_val;
    const std::string str = "123456789.123456";  // 15 significant digits, more than decimal32_t can hold
    std::span<const unsigned char> data(reinterpret_cast<const unsigned char*>(str.data()), str.size());
    auto err = types::parse_text_decimal(data, out_val);
    NATIVEPG_TEST_EQ(err, boost::system::error_code{});
    NATIVEPG_TEST_EQ(out_val, bd::decimal32_t{str});
}

}  // namespace

int main()
{
    // NUMERIC / DECIMAL
    // 1234.5678
    static constexpr unsigned char pg_num_1234_5678[] =
        {0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x04, 0xD2, 0x16, 0x2E};
    // 0  -> ndigits=0 weight=0 sign=0x0000 dscale=0 (no digit groups)
    static constexpr unsigned char pg_num_zero[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    // -1234.5678 -> same as above but sign=0x4000
    static constexpr unsigned char pg_num_neg[] =
        {0x00, 0x02, 0x00, 0x00, 0x40, 0x00, 0x00, 0x04, 0x04, 0xD2, 0x16, 0x2E};
    // sign=0xC000 => NaN
    static constexpr unsigned char pg_num_nan[] = {0x00, 0x00, 0x00, 0x00, 0xC0, 0x00, 0x00, 0x00};
    // sign=0xD000 => Infinity
    static constexpr unsigned char pg_num_infinity_pos[] = {0x00, 0x00, 0x00, 0x00, 0xD0, 0x00, 0x00, 0x00};
    // sign=0xF000 => -Infinity
    static constexpr unsigned char pg_num_infinity_neg[] = {0x00, 0x00, 0x00, 0x00, 0xF0, 0x00, 0x00, 0x00};
    // 0.001 -> ndigits=1, weight=-1, sign=0x0000, dscale=3, group 0=10 (10 * 10000^-1 = 0.001)
    static constexpr unsigned char pg_num_frac[] =
        {0x00, 0x01, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x03, 0x00, 0x0A};
    // Same digits as pg_num_1234_5678 but dscale=2, forcing real rounding: 1234.5678 -> 1234.57
    static constexpr unsigned char pg_num_round[] =
        {0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x04, 0xD2, 0x16, 0x2E};
    // Header-only buffer, too short to even contain a valid 8-byte header.
    static constexpr unsigned char pg_too_short[] = {0x00, 0x01, 0x00, 0x00};
    // Declares ndigits=2 but only provides 1 digit group (10 bytes instead of the required 12).
    static constexpr unsigned char pg_truncated[] =
        {0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x04, 0xD2};

    using d32 = bd::decimal32_t;
    test_parse_text_decimal_success(d32{"1234567.99"});
    test_parse_text_decimal_success(std::numeric_limits<d32>::max());
    test_parse_text_decimal_success(std::numeric_limits<d32>::min());
    test_parse_text_decimal_success(std::numeric_limits<d32>::quiet_NaN());
    test_parse_binary_decimal_success(pg_num_1234_5678, d32{1234.56});
    test_parse_binary_decimal_success(pg_num_zero, d32{0});
    test_parse_binary_decimal_success(pg_num_neg, d32{-1234.56});
    test_parse_binary_decimal_success(pg_num_nan, d32("NaN"));
    test_parse_binary_decimal_success(pg_num_infinity_pos, d32("inf"));
    test_parse_binary_decimal_success(pg_num_infinity_neg, d32("-inf"));
    test_parse_binary_decimal_success(pg_num_frac, d32("0.001"));
    test_parse_binary_decimal_success(pg_num_round, d32("1234.57"));
    test_parse_text_decimal_from_str("000123.45", d32("123.45"));
    test_parse_text_decimal_from_str("inf", d32("inf"));
    test_parse_text_decimal_from_str("-inf", d32("-inf"));

    using d64 = bd::decimal64_t;
    test_parse_text_decimal_success(d64{"1234567.99"});
    test_parse_text_decimal_success(std::numeric_limits<d64>::max());
    test_parse_text_decimal_success(std::numeric_limits<d64>::min());
    test_parse_text_decimal_success(std::numeric_limits<d64>::quiet_NaN());
    test_parse_binary_decimal_success(pg_num_1234_5678, d64{1234.5678});
    test_parse_binary_decimal_success(pg_num_zero, d64{0});
    test_parse_binary_decimal_success(pg_num_neg, d64{-1234.5678});
    test_parse_binary_decimal_success(pg_num_nan, d64("NaN"));
    test_parse_binary_decimal_success(pg_num_infinity_pos, d64("inf"));
    test_parse_binary_decimal_success(pg_num_infinity_neg, d64("-inf"));
    test_parse_binary_decimal_success(pg_num_frac, d64("0.001"));
    test_parse_binary_decimal_success(pg_num_round, d64("1234.57"));

    using d128 = bd::decimal128_t;
    test_parse_text_decimal_success(d128{"1234567.99"});
    test_parse_text_decimal_success(std::numeric_limits<d128>::max());
    test_parse_text_decimal_success(std::numeric_limits<d128>::min());
    test_parse_text_decimal_success(std::numeric_limits<d128>::quiet_NaN());
    test_parse_binary_decimal_success(pg_num_1234_5678, d128{1234.5678});
    test_parse_binary_decimal_success(pg_num_zero, d128{0});
    test_parse_binary_decimal_success(pg_num_neg, d128{-1234.5678});
    test_parse_binary_decimal_success(pg_num_nan, d128("NaN"));
    test_parse_binary_decimal_success(pg_num_infinity_pos, d128("inf"));
    test_parse_binary_decimal_success(pg_num_infinity_neg, d128("-inf"));
    test_parse_binary_decimal_success(pg_num_frac, d128("0.001"));
    test_parse_binary_decimal_success(pg_num_round, d128("1234.57"));

    // Malformed text input: must not throw out of parse_text_decimal, and must report protocol_value_error.
    const boost::system::error_code parse_error(client_errc::protocol_value_error);
    test_parse_text_decimal_error<d32>("abc", parse_error);
    test_parse_text_decimal_error<d32>("", parse_error);
    test_parse_text_decimal_error<d32>("12.3.4", parse_error);
    // Note: unlike numeric.hpp's cpp_dec_float text parsing (which tolerates a leading '+'),
    // boost::decimal::from_chars follows std::from_chars semantics and rejects one.
    test_parse_text_decimal_error<d32>("+123.45", parse_error);
    test_parse_text_decimal_precision_loss();

    // Malformed / truncated binary input.
    test_parse_binary_decimal_error<d32>(pg_too_short, parse_error);
    test_parse_binary_decimal_error<d32>(pg_truncated, parse_error);

    // detail::field_is_compatible / detail::field_parse (field_traits_decimal.hpp)
    test_field_is_compatible_decimal_success();
    test_field_is_compatible_decimal_incompatible_error();
    test_field_parse_decimal_text_success();
    test_field_parse_decimal_binary_success();

    return boost::report_errors();
};
