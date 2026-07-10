//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#define NATIVEPG_USE_NUMERIC_TYPES 1

#include <boost/core/lightweight_test.hpp>
#include <boost/system/error_code.hpp>

#include <string>
#include <sstream>
#include <iomanip>
#include <limits>
#include <span>

#include "nativepg/detail/field_traits.hpp"
#include "nativepg/protocol/describe.hpp"

#include "nativepg/types/numeric.hpp"

using namespace nativepg;
namespace mp = boost::multiprecision;

namespace {

template <std::size_t TDigits, typename T = mp::number<mp::cpp_dec_float<TDigits>>>
void test_parse_text_numeric_success(const T& in_val)
{
    // Arrange
    T out_val;
    std::stringstream ss;
    ss << std::setprecision(std::numeric_limits<T>::max_digits10) << in_val;
    std::string str = ss.str();
    boost::span<const unsigned char> data(reinterpret_cast<const unsigned char*>(str.data()), str.size());

    // Act
    auto err = types::parse_text_numeric<TDigits, T>(data, out_val);

    // Assert
    BOOST_TEST_EQ(err, boost::system::errc::success);
    if (boost::math::isnan(in_val))
        BOOST_TEST(boost::math::isnan(out_val));   // NaN != NaN, so compare by predicate
    else
        BOOST_TEST_EQ(out_val, in_val);
}

template <std::size_t TDigits, typename T = mp::number<mp::cpp_dec_float<TDigits>>>
void test__parse_binary_numeric__success(std::span<const unsigned char> wire, const T& expected)
{
    // Arrange
    T out_val;

    // Act
    auto err = types::parse_binary_numeric<TDigits, T>(wire, out_val);

    // Assert
    BOOST_TEST_EQ(err, boost::system::errc::success);
    if (boost::math::isnan(expected))
        BOOST_TEST(boost::math::isnan(out_val));   // NaN != NaN, so compare by predicate
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
    //A larger sanity check — 100000000 (= 1·10000^2), ndigits=1, weight=2, dscale=0, group 1=0x0001:
    static constexpr unsigned char pg_num_1e8[] = {
        0x00, 0x01,  0x00, 0x02,  0x00, 0x00,  0x00, 0x00,
        0x00, 0x01
    };
    using dec50 = boost::multiprecision::cpp_dec_float_50;
    test_parse_text_numeric_success<50>(std::numeric_limits<dec50>::min());
    test_parse_text_numeric_success<50>(std::numeric_limits<dec50>::max());
    test_parse_text_numeric_success<50>(std::numeric_limits<dec50>::quiet_NaN());
    test__parse_binary_numeric__success<50>(pg_num_1234_5678, dec50("1234.5678"));
    test__parse_binary_numeric__success<50>(pg_num_zero, dec50(0));
    test__parse_binary_numeric__success<50>(pg_num_neg, dec50("-1234.5678"));
    test__parse_binary_numeric__success<50>(pg_num_nan, dec50("NaN"));
    test__parse_binary_numeric__success<50>(pg_num_infinity_pos, dec50("inf"));
    test__parse_binary_numeric__success<50>(pg_num_infinity_neg, dec50("-inf"));
    test__parse_binary_numeric__success<50>(pg_num_1e8, dec50("100000000"));
    using dec100 = boost::multiprecision::cpp_dec_float_100;
    test_parse_text_numeric_success<100>(std::numeric_limits<dec100>::min());
    test_parse_text_numeric_success<100>(std::numeric_limits<dec100>::max());
    test_parse_text_numeric_success<100>(std::numeric_limits<dec100>::quiet_NaN());
    test__parse_binary_numeric__success<100>(pg_num_1234_5678, dec100("1234.5678"));
    test__parse_binary_numeric__success<100>(pg_num_zero, dec100(0));
    test__parse_binary_numeric__success<100>(pg_num_neg, dec100("-1234.5678"));
    test__parse_binary_numeric__success<100>(pg_num_nan, dec100("NaN"));
    test__parse_binary_numeric__success<100>(pg_num_infinity_pos, dec100("inf"));
    test__parse_binary_numeric__success<100>(pg_num_infinity_neg, dec100("-inf"));
    test__parse_binary_numeric__success<100>(pg_num_1e8, dec100("100000000"));

    return boost::report_errors();
}
