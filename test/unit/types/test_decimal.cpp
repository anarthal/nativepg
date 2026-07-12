//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#define NATIVEPG_USE_DECIMAL_TYPES 1

#include <boost/core/lightweight_test.hpp>
#include <boost/system/error_code.hpp>
#include <boost/decimal.hpp>

#include <string>
#include <sstream>
#include <iomanip>
#include <limits>
#include <span>

#include "nativepg/types/decimal.hpp"
#include "nativepg/detail/field_traits.hpp"
#include "nativepg/protocol/describe.hpp"


using namespace nativepg;
namespace bd = boost::decimal;

namespace {

template <typename T>
    requires std::same_as<T, bd::decimal32_t> ||
             std::same_as<T, bd::decimal64_t> ||
             std::same_as<T, bd::decimal128_t>
void test_parse_text_decimal_success(const T& in_val)
{
    // Arrange
    T out_val;
    std::stringstream ss;
    ss << std::setprecision(std::numeric_limits<T>::max_digits10) << in_val;
    const std::string str = ss.str();
    boost::span<const unsigned char> data(reinterpret_cast<const unsigned char*>(str.data()), str.size());

    // Act
    auto err = types::parse_text_decimal(data, out_val);

    // Assert
    BOOST_TEST_EQ(err, boost::system::errc::success);
    if (bd::isnan(in_val))
        BOOST_TEST(bd::isnan(out_val));   // NaN != NaN, so compare by predicate
    else
        BOOST_TEST_EQ(out_val, in_val);
}

template <typename T>
    requires std::same_as<T, bd::decimal32_t> ||
             std::same_as<T, bd::decimal64_t> ||
             std::same_as<T, bd::decimal128_t>
void test_parse_binary_decimal_success(std::span<const unsigned char> wire, const T& expected)
{
    // Arrange
    T out_val;

    // Act
    auto err = types::parse_binary_decimal(wire, out_val);

    // Assert
    BOOST_TEST_EQ(err, boost::system::errc::success);
    if (bd::isnan(expected))
        BOOST_TEST(bd::isnan(out_val));   // NaN != NaN, so compare by predicate
    else
        BOOST_TEST_EQ(out_val, expected);
}

}  // namespace

int main()
{
    // NUMERIC / DECIMAL
    // 1234.5678
    static constexpr unsigned char pg_num_1234_5678[] = {
        0x00, 0x02,  0x00, 0x00,  0x00, 0x00,  0x00, 0x04,
        0x04, 0xD2,  0x16, 0x2E
    };
    // 0  -> ndigits=0 weight=0 sign=0x0000 dscale=0 (no digit groups)
    static constexpr unsigned char pg_num_zero[] = {
        0x00, 0x00,  0x00, 0x00,  0x00, 0x00,  0x00, 0x00
    };
    // -1234.5678 -> same as above but sign=0x4000
    static constexpr unsigned char pg_num_neg[] = {
        0x00, 0x02,  0x00, 0x00,  0x40, 0x00,  0x00, 0x04,
        0x04, 0xD2,  0x16, 0x2E
    };
    // sign=0xC000 => NaN
    static constexpr unsigned char pg_num_nan[] = {
        0x00, 0x00, 0x00, 0x00, 0xC0, 0x00, 0x00, 0x00
    };
    // sign=0xD000 => Infinity
    static constexpr unsigned char pg_num_infinity_pos[] = {
        0x00, 0x00, 0x00, 0x00, 0xD0, 0x00, 0x00, 0x00
    };
    // sign=0xF000 => -Infinity
    static constexpr unsigned char pg_num_infinity_neg[] = {
        0x00, 0x00, 0x00, 0x00, 0xF0, 0x00, 0x00, 0x00
    };
    using d32 = bd::decimal32_t;
    test_parse_text_decimal_success(d32{"1234567.99"});
    test_parse_text_decimal_success(std::numeric_limits<d32>::max());
    test_parse_text_decimal_success(std::numeric_limits<d32>::min());
    test_parse_text_decimal_success(std::numeric_limits<d32>::quiet_NaN());
    test_parse_binary_decimal_success(pg_num_1234_5678, d32{1234.56});
    test_parse_binary_decimal_success(pg_num_zero, d32{0});
    test_parse_binary_decimal_success(pg_num_neg , d32{-1234.56});
    test_parse_binary_decimal_success(pg_num_nan, d32("NaN"));
    test_parse_binary_decimal_success(pg_num_infinity_pos, d32("inf"));
    test_parse_binary_decimal_success(pg_num_infinity_neg, d32("-inf"));

    using d64 = bd::decimal64_t;
    test_parse_text_decimal_success(d64{"1234567.99"});
    test_parse_text_decimal_success(std::numeric_limits<d64>::max());
    test_parse_text_decimal_success(std::numeric_limits<d64>::min());
    test_parse_text_decimal_success(std::numeric_limits<d64>::quiet_NaN());
    test_parse_binary_decimal_success(pg_num_1234_5678, d64{1234.5678});
    test_parse_binary_decimal_success(pg_num_zero, d64{0});
    test_parse_binary_decimal_success(pg_num_neg , d64{-1234.5678});
    test_parse_binary_decimal_success(pg_num_nan, d64("NaN"));
    test_parse_binary_decimal_success(pg_num_infinity_pos, d64("inf"));
    test_parse_binary_decimal_success(pg_num_infinity_neg, d64("-inf"));

    using d128 = bd::decimal128_t;
    test_parse_text_decimal_success(d128{"1234567.99"});
    test_parse_text_decimal_success(std::numeric_limits<d128>::max());
    test_parse_text_decimal_success(std::numeric_limits<d128>::min());
    test_parse_text_decimal_success(std::numeric_limits<d128>::quiet_NaN());
    test_parse_binary_decimal_success(pg_num_1234_5678, d128{1234.5678});
    test_parse_binary_decimal_success(pg_num_zero, d128{0});
    test_parse_binary_decimal_success(pg_num_neg , d128{-1234.5678});
    test_parse_binary_decimal_success(pg_num_nan, d128("NaN"));
    test_parse_binary_decimal_success(pg_num_infinity_pos, d128("inf"));
    test_parse_binary_decimal_success(pg_num_infinity_neg, d128("-inf"));

    return boost::report_errors();
};
