//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/core/lightweight_test.hpp>
#include <boost/system/error_code.hpp>

#include <chrono>
#include <format>
#include <sstream>
#include <string>

#include "nativepg/detail/field_traits.hpp"
#include "nativepg/protocol/describe.hpp"

#include "nativepg/types/datetime.hpp"

using namespace nativepg;

namespace {

// DATE
void test__parse_text_date__success()
{
    // Arrange
    types::pg_date d;
    std::string str = "1977-06-21";
    boost::span<const unsigned char> data(reinterpret_cast<const unsigned char*>(str.data()), str.size());
    std::stringstream ss;

    // Act
    auto err = types::parse_text_date(data, d);

    // Assert
    BOOST_TEST_EQ(err, boost::system::errc::success);

    ss << std::format("{:%F}", d) << std::flush;
    BOOST_TEST_EQ(ss.str(), str);
}

void test__parse_binary_date__success()
{
    // Arrange
    types::pg_date d;
    std::string str = "1977-06-21";
    static constexpr unsigned char pg_date_big_endians[] = {
        0xFF, 0xFF, 0xDF, 0xDB
    };
    boost::span<const unsigned char> data(pg_date_big_endians);
    std::stringstream ss;

    // Act
    auto err = types::parse_binary_date(data, d);

    // Assert
    BOOST_TEST_EQ(err, boost::system::errc::success);

    ss << std::format("{:%F}", d) << std::flush;
    BOOST_TEST_EQ(ss.str(), str);
}


// TIME
void test__parse_text_time__success()
{
    // Arrange
    std::chrono::microseconds us;
    std::string str = "21:06:19";
    boost::span<const unsigned char> data(reinterpret_cast<const unsigned char*>(str.data()), str.size());
    std::stringstream ss;

    // Act
    auto err = types::parse_text_time(data, us);

    // Assert
    BOOST_TEST_EQ(err, boost::system::errc::success);

    ss << std::format("{:%T}", std::chrono::duration_cast<std::chrono::seconds>(us)) << std::flush;
    BOOST_TEST_EQ(ss.str(), str);
}

void test__parse_binary_time__success()
{
    // Arrange
    std::chrono::microseconds us;
    std::string str = "21:06:19";
    // 21:06:19 as bigendian microseconds data
    static constexpr unsigned char pg_time_210619[] = {
        0x00, 0x00, 0x00, 0x11,
        0xB0, 0xB3, 0x88, 0xC0
    };
    boost::span<const unsigned char> data(pg_time_210619);
    std::stringstream ss;

    // Act
    auto err = types::parse_binary_time(data, us);

    // Assert
    BOOST_TEST_EQ(err, boost::system::errc::success);

    ss << std::format("{:%T}", std::chrono::duration_cast<std::chrono::seconds>(us)) << std::flush;
    BOOST_TEST_EQ(ss.str(), str);
}


// TIMETZ
void test__parse_text_timetz__success()
{
    // Arrange
    types::pg_timetz tz;
    std::string str = "21:06:19+07:00";
    boost::span<const unsigned char> data(reinterpret_cast<const unsigned char*>(str.data()), str.size());
    std::stringstream ss;

    // Act
    auto err = types::parse_text_timetz(data, tz);

    // Assert
    BOOST_TEST_EQ(err, boost::system::errc::success);

    ss << std::format("{0:%T}+{1:%H:%M}", std::chrono::duration_cast<std::chrono::seconds>(tz.time_since_midnight), std::chrono::duration_cast<std::chrono::minutes>(tz.utc_offset)) << std::flush;
    BOOST_TEST_EQ(ss.str(), str);
}

void test__parse_binary_timetz__success()
{
    // Arrange
    types::pg_timetz tz;
    std::string str = "12:34:23.435350+05:00";
    static constexpr unsigned char pg_time_be[] = {
        0x00, 0x00, 0x00, 0x0A, 0x89, 0xe9, 0x36, 0x56, 0xff, 0xff, 0xb9, 0xb0 };
    boost::span<const unsigned char> data(pg_time_be);
    std::stringstream ss;

    // Act
    auto err = types::parse_binary_timetz(data, tz);

    // Assert
    BOOST_TEST_EQ(err, boost::system::errc::success);

    ss << std::format("{0:%T}+{1:%H:%M}", tz.time_since_midnight, tz.utc_offset) << std::flush;
    BOOST_TEST_EQ(ss.str(), str);
}

// TIMESTAMP
void test__parse_text_timestamp__success()
{
    // Arrange
    types::pg_timestamp ts;
    std::string str = "1977-06-21 21:06:19";
    boost::span<const unsigned char> data(reinterpret_cast<const unsigned char*>(str.data()), str.size());
    std::stringstream ss;

    // Act
    auto err = types::parse_text_timestamp(data, ts);

    // Assert
    BOOST_TEST_EQ(err, boost::system::errc::success);

    std::chrono::local_time<std::chrono::seconds> t_s = std::chrono::time_point_cast<std::chrono::seconds>(ts);
    ss << std::format("{0:%F} {1:%T}", ts, t_s) << std::flush;
    BOOST_TEST_EQ(ss.str(), str);
}

void test__parse_binary_timestamp__success()
{
    // Arrange
    types::pg_timestamp ts;
    std::string str = "2026-02-08 12:34:23.435350";
    static constexpr unsigned char pg_time_be[] = {
        0x00, 0x02, 0xed, 0x4E, 0x02, 0xc9, 0xd6, 0x56
    };
    boost::span<const unsigned char> data(pg_time_be);
    std::stringstream ss;

    // Act
    auto err = types::parse_binary_timestamp(data, ts);

    // Assert
    BOOST_TEST_EQ(err, boost::system::errc::success);

    ss << std::format("{0:%F} {0:%H:%M:%S}", ts) << std::flush;
    BOOST_TEST_EQ(ss.str(), str);
}

// TIMESTAMPTZ
void test__parse_text_timestamptz__success()
{
    // Arrange
    types::pg_timestamptz ts;
    std::string str = "2026-02-08 20:03:00+00:00";
    boost::span<const unsigned char> data(reinterpret_cast<const unsigned char*>(str.data()), str.size());
    std::stringstream ss;

    // Act
    auto err = types::parse_text_timestamptz(data, ts);

    // Assert
    BOOST_TEST_EQ(err, boost::system::errc::success);

    // We only check the year/month/day/hour/minute/second as timezone handling in format is tricky
    ss << std::format("{0:%F} {0:%H:%M:%S}", std::chrono::time_point_cast<std::chrono::seconds>(ts)) << std::flush;
    BOOST_TEST_EQ(ss.str(), "2026-02-08 20:03:00");
}

void test__parse_binary_timestamptz__success()
{
    // Arrange
    types::pg_timestamptz ts;
    std::string str = "2026-02-08 12:34:23.435350";
    static constexpr unsigned char pg_time_be[] = {
        0x00, 0x02, 0xed, 0x4E, 0x02, 0xc9, 0xd6, 0x56
    };
    boost::span<const unsigned char> data(pg_time_be);
    std::stringstream ss;

    // Act
    auto err = types::parse_binary_timestamptz(data, ts);

    // Assert
    BOOST_TEST_EQ(err, boost::system::errc::success);

    ss << std::format("{0:%F} {0:%H:%M:%S}", ts) << std::flush;
    BOOST_TEST_EQ(ss.str(), str);
}

// INTERVAL
void test__parse_text_interval__success()
{
    // Arrange
    types::pg_interval inv;
    std::string str = "1 year 2 mons 3 days 04:05:06.000007";
    boost::span<const unsigned char> data(reinterpret_cast<const unsigned char*>(str.data()), str.size());

    // Act
    auto err = types::parse_text_interval(data, inv);

    // Assert
    BOOST_TEST_EQ(err, boost::system::errc::success);
    BOOST_TEST_EQ(inv.months, 14);
    BOOST_TEST_EQ(inv.days, 3);
    BOOST_TEST_EQ(inv.time.count(), (std::chrono::hours(4) + std::chrono::minutes(5) + std::chrono::seconds(6) + std::chrono::microseconds(7)).count());
}

void test__parse_binary_interval__success()
{
    // Arrange
    types::pg_interval inv;
    // months: 1 = 0x00000001
    // days: 1 = 0x00000001
    // time: 1 microsecond = 0x0000000000000001
    static constexpr unsigned char pg_interval_be[] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, // time (8 bytes)
        0x00, 0x00, 0x00, 0x01,                         // days (4 bytes)
        0x00, 0x00, 0x00, 0x01                          // months (4 bytes)
    };
    boost::span<const unsigned char> data(pg_interval_be);

    // Act
    auto err = types::parse_binary_interval(data, inv);

    // Assert
    BOOST_TEST_EQ(err, boost::system::errc::success);
    BOOST_TEST_EQ(inv.months, 1);
    BOOST_TEST_EQ(inv.days, 1);
    BOOST_TEST_EQ(inv.time.count(), 1);
}
}  // namespace

int main()
{
    test__parse_text_date__success();
    test__parse_binary_date__success();

    test__parse_text_time__success();
    test__parse_binary_time__success();

    test__parse_text_timetz__success();
    test__parse_binary_timetz__success();

    test__parse_text_timestamp__success();
    test__parse_binary_timestamp__success();

    test__parse_text_timestamptz__success();
    test__parse_binary_timestamptz__success();

    test__parse_text_interval__success();
    test__parse_binary_interval__success();

    return boost::report_errors();
}
