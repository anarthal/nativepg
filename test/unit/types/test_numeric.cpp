//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/core/lightweight_test.hpp>
#include <boost/multiprecision/cpp_dec_float.hpp>
#include <boost/multiprecision/number.hpp>
#include <boost/system/error_code.hpp>

#include <cstdint>
#include <iomanip>
#include <limits>
#include <span>
#include <sstream>
#include <string>

#include "nativepg/detail/field_traits.hpp"
#include "nativepg/protocol/describe.hpp"
#include "nativepg/types/numeric.hpp"
#include "test_utils.hpp"

using namespace nativepg;
namespace mp = boost::multiprecision;

namespace {

template <const std::size_t TDigits, typename T = mp::number<mp::cpp_dec_float<TDigits>>>
void test_parse_text_numeric_success(const T& in_val)
{
    // Arrange
    T out_val;
    std::stringstream ss;
    ss << std::setprecision(std::numeric_limits<T>::max_digits10) << in_val;
    const std::string str = ss.str();
    std::span<const unsigned char> data(reinterpret_cast<const unsigned char*>(str.data()), str.size());

    // Act
    auto err = types::parse_text_numeric(data, out_val);

    // Assert
    NATIVEPG_TEST_EQ(err, boost::system::errc::success);
    if (boost::math::isnan(in_val))
    {
        NATIVEPG_TEST(boost::math::isnan(out_val));  // NaN != NaN, so compare by predicate
    }
    else
    {
        NATIVEPG_TEST_EQ(out_val, in_val);
    }
}

template <std::size_t TDigits, typename T = mp::number<mp::cpp_dec_float<TDigits>>>
void test_parse_binary_numeric_success(std::span<const unsigned char> wire, const T& expected)
{
    // Arrange
    T out_val;

    // Act
    auto err = types::parse_binary_numeric(wire, out_val);

    // Assert
    NATIVEPG_TEST_EQ(err, boost::system::errc::success);
    if (boost::math::isnan(expected))
    {
        NATIVEPG_TEST(boost::math::isnan(out_val));  // NaN != NaN, so compare by predicate
    }
    else
    {
        NATIVEPG_TEST_EQ(out_val, expected);
    }
}

template <const std::size_t TDigits, class T = mp::number<mp::cpp_dec_float<TDigits>>>
void test_parse_text_numeric_digits_fit(const std::string& str, boost::system::error_code expected)
{
    // Arrange
    T out_val;
    std::span<const unsigned char> data(reinterpret_cast<const unsigned char*>(str.data()), str.size());

    // Act
    auto ec = types::parse_text_numeric(data, out_val);

    // Assert
    NATIVEPG_TEST_EQ(ec.value(), expected.value());
}

// Verifies a text literal (e.g. one with a leading '+' sign, or leading/trailing zeros) parses to the
// expected value. Unlike test_parse_text_numeric_success, the wire text is given verbatim rather than
// derived from streaming `expected`, so it can cover forms the stream operator never produces.
template <const std::size_t TDigits, class T = mp::number<mp::cpp_dec_float<TDigits>>>
void test_parse_text_numeric_from_str(const std::string& str, const T& expected)
{
    // Arrange
    T out_val;
    std::span<const unsigned char> data(reinterpret_cast<const unsigned char*>(str.data()), str.size());

    // Act
    auto err = types::parse_text_numeric(data, out_val);

    // Assert
    NATIVEPG_TEST_EQ(err, boost::system::errc::success);
    NATIVEPG_TEST_EQ(out_val, expected);
}

template <std::size_t TDigits, typename T = mp::number<mp::cpp_dec_float<TDigits>>>
void test_parse_binary_numeric_digits_fit(
    std::span<const unsigned char> wire,
    boost::system::error_code expected
)
{
    // Arrange
    T out_val;

    // Act
    auto ec = types::parse_binary_numeric(wire, out_val);

    // Assert
    NATIVEPG_TEST_EQ(ec.value(), expected.value());
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
// detail::field_is_compatible / detail::field_parse (field_traits_numeric.hpp)
//
void test_field_is_compatible_numeric_success()
{
    NATIVEPG_TEST_EQ(
        detail::field_is_compatible<mp::number<mp::cpp_dec_float<50>>>::call(
            make_field_description(detail::numeric_oid)
        ),
        boost::system::errc::success
    );
}

void test_field_is_compatible_numeric_incompatible_error()
{
    NATIVEPG_TEST_EQ(
        detail::field_is_compatible<mp::number<mp::cpp_dec_float<50>>>::call(
            make_field_description(23 /* int4 oid */)
        ),
        boost::system::error_code(client_errc::incompatible_field_type)
    );
}

void test_field_parse_numeric_text_success()
{
    // Arrange
    mp::number<mp::cpp_dec_float<50>> out_val;
    const std::string str = "1234.5678";
    std::span<const unsigned char> data(reinterpret_cast<const unsigned char*>(str.data()), str.size());
    field_view fv{data};
    const auto desc = make_field_description(detail::numeric_oid, protocol::format_code::text);

    // Act
    auto err = detail::field_parse<mp::number<mp::cpp_dec_float<50>>>::call(fv, desc, out_val);

    // Assert
    NATIVEPG_TEST_EQ(err, boost::system::errc::success);
    NATIVEPG_TEST_EQ(out_val, mp::number<mp::cpp_dec_float<50>>("1234.5678"));
}

void test_field_parse_numeric_binary_success()
{
    // Arrange
    mp::number<mp::cpp_dec_float<50>> out_val;
    // 1234.5678: ndigits=2, weight=0, sign=0x0000, dscale=4, groups 1234/5678
    static constexpr unsigned char pg_num_1234_5678[] =
        {0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x04, 0xD2, 0x16, 0x2E};
    field_view fv{pg_num_1234_5678};
    const auto desc = make_field_description(detail::numeric_oid, protocol::format_code::binary);

    // Act
    auto err = detail::field_parse<mp::number<mp::cpp_dec_float<50>>>::call(fv, desc, out_val);

    // Assert
    NATIVEPG_TEST_EQ(err, boost::system::errc::success);
    NATIVEPG_TEST_EQ(out_val, mp::number<mp::cpp_dec_float<50>>("1234.5678"));
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
    // A larger sanity check — 100000000 (= 1·10000^2), ndigits=1, weight=2, dscale=0, group 1=0x0001:
    static constexpr unsigned char pg_num_1e8[] =
        {0x00, 0x01, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01};
    // 123456789012345 -> ndigits=4, weight=3, sign=0x0000, dscale=0
    // base-10000 groups (MSB first): 123, 4567, 8901, 2345
    static constexpr unsigned char pg_15digits[] = {
        0x00,
        0x04,
        0x00,
        0x03,
        0x00,
        0x00,
        0x00,
        0x00,  // header
        0x00,
        0x7B,
        0x11,
        0xD7,
        0x22,
        0xC5,
        0x09,
        0x29,  // 123 4567 8901 2345
    };
    // 0.001 -> ndigits=1, weight=-1, sign=0x0000, dscale=3, group 0=10 (10 * 10000^-1 = 0.001)
    static constexpr unsigned char pg_num_frac[] =
        {0x00, 0x01, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x03, 0x00, 0x0A};
    // 1200 -> ndigits=1, weight=0, sign=0x0000, dscale=0, group 0=1200 (0x04B0).
    // Exercises count_significant_digits_binary's trailing-zero-within-the-last-group loop: "1200" has
    // 2 significant digits ("12"), not 4.
    static constexpr unsigned char pg_num_1200[] =
        {0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0xB0};
    // Same digits as pg_num_1234_5678 but dscale=2, forcing real rounding: 1234.5678 -> 1234.57
    static constexpr unsigned char pg_num_round[] =
        {0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x04, 0xD2, 0x16, 0x2E};
    // 12345678910 -> ndigits=3, weight=2, groups 123/4567/8910. Naive digit count is 11, but the last
    // group ends in a zero, so count_significant_digits_binary must strip it down to 10 significant
    // digits ("1234567891"). Exercises the trailing-zero-within-the-last-group loop at a boundary that's
    // actually reachable given cpp_dec_float's minimum digits10 of 9.
    static constexpr unsigned char pg_num_10digits_trailing_zero[] =
        {0x00, 0x03, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7B, 0x11, 0xD7, 0x22, 0xCE};
    // Header-only buffer, too short to even contain a valid 8-byte header.
    static constexpr unsigned char pg_too_short[] = {0x00, 0x01, 0x00, 0x00};
    // Declares ndigits=2 but only provides 1 digit group (10 bytes instead of the required 12).
    static constexpr unsigned char pg_truncated[] =
        {0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x04, 0xD2};

    using dec50 = boost::multiprecision::cpp_dec_float_50;
    test_parse_text_numeric_success<50>(std::numeric_limits<dec50>::min());
    test_parse_text_numeric_success<50>(std::numeric_limits<dec50>::max());
    test_parse_text_numeric_success<50>(std::numeric_limits<dec50>::quiet_NaN());
    test_parse_binary_numeric_success<50>(pg_num_1234_5678, dec50("1234.5678"));
    test_parse_binary_numeric_success<50>(pg_num_zero, dec50(0));
    test_parse_binary_numeric_success<50>(pg_num_neg, dec50("-1234.5678"));
    test_parse_binary_numeric_success<50>(pg_num_nan, dec50("NaN"));
    test_parse_binary_numeric_success<50>(pg_num_infinity_pos, dec50("inf"));
    test_parse_binary_numeric_success<50>(pg_num_infinity_neg, dec50("-inf"));
    test_parse_binary_numeric_success<50>(pg_num_1e8, dec50("100000000"));
    test_parse_binary_numeric_success<50>(pg_num_frac, dec50("0.001"));
    test_parse_binary_numeric_success<50>(pg_num_1200, dec50("1200"));
    test_parse_binary_numeric_success<50>(pg_num_round, dec50("1234.57"));
    test_parse_text_numeric_from_str<50>("+123.45", dec50("123.45"));
    test_parse_text_numeric_from_str<50>("000123.45", dec50("123.45"));
    test_parse_text_numeric_from_str<50>("inf", dec50("inf"));
    test_parse_text_numeric_from_str<50>("-inf", dec50("-inf"));
    using dec100 = boost::multiprecision::cpp_dec_float_100;
    test_parse_text_numeric_success<100>(std::numeric_limits<dec100>::min());
    test_parse_text_numeric_success<100>(std::numeric_limits<dec100>::max());
    test_parse_text_numeric_success<100>(std::numeric_limits<dec100>::quiet_NaN());
    test_parse_binary_numeric_success<100>(pg_num_1234_5678, dec100("1234.5678"));
    test_parse_binary_numeric_success<100>(pg_num_zero, dec100(0));
    test_parse_binary_numeric_success<100>(pg_num_neg, dec100("-1234.5678"));
    test_parse_binary_numeric_success<100>(pg_num_nan, dec100("NaN"));
    test_parse_binary_numeric_success<100>(pg_num_infinity_pos, dec100("inf"));
    test_parse_binary_numeric_success<100>(pg_num_infinity_neg, dec100("-inf"));
    test_parse_binary_numeric_success<100>(pg_num_1e8, dec100("100000000"));
    test_parse_binary_numeric_success<100>(pg_num_frac, dec100("0.001"));
    test_parse_binary_numeric_success<100>(pg_num_1200, dec100("1200"));
    test_parse_binary_numeric_success<100>(pg_num_round, dec100("1234.57"));

    // Check types that do not fit
    const boost::system::error_code failure = nativepg::make_error_code(
        client_errc::incompatible_response_length
    );
    test_parse_text_numeric_digits_fit<14>("123456789012345", failure);
    test_parse_binary_numeric_digits_fit<14>(pg_15digits, failure);
    // 10 significant digits (after trailing-zero stripping) must not fit in a 9-digit type.
    test_parse_binary_numeric_digits_fit<9>(pg_num_10digits_trailing_zero, failure);

    // Check types that fit
    const boost::system::error_code success{};
    test_parse_text_numeric_digits_fit<16>("123456789012345", success);
    test_parse_binary_numeric_digits_fit<16>(pg_15digits, success);
    // Exact-boundary case: significant_digits == type_digits must still succeed (check is strictly `>`).
    test_parse_text_numeric_digits_fit<15>("123456789012345", success);
    test_parse_binary_numeric_digits_fit<15>(pg_15digits, success);
    test_parse_binary_numeric_digits_fit<10>(pg_num_10digits_trailing_zero, success);
    // "1200" has only 2 significant digits ("12"); trivially fits even the smallest usable type.
    test_parse_binary_numeric_digits_fit<9>(pg_num_1200, success);

    // Malformed text input: must not throw out of parse_text_numeric, and must report protocol_value_error.
    const boost::system::error_code parse_error(client_errc::protocol_value_error);
    test_parse_text_numeric_digits_fit<50>("abc", parse_error);
    test_parse_text_numeric_digits_fit<50>("", parse_error);
    test_parse_text_numeric_digits_fit<50>("12.3.4", parse_error);

    // Malformed / truncated binary input.
    test_parse_binary_numeric_digits_fit<50>(pg_too_short, parse_error);
    test_parse_binary_numeric_digits_fit<50>(pg_truncated, parse_error);

    // detail::field_is_compatible / detail::field_parse (field_traits_numeric.hpp)
    test_field_is_compatible_numeric_success();
    test_field_is_compatible_numeric_incompatible_error();
    test_field_parse_numeric_text_success();
    test_field_parse_numeric_binary_success();

    return boost::report_errors();
}
