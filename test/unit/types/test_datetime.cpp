//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/core/lightweight_test.hpp>

#include <chrono>
#include <format>
#include <source_location>
#include <span>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#include "nativepg/client_errc.hpp"
#include "nativepg/field_traits.hpp"
#include "nativepg/types/datetime.hpp"
#include "test_utils/test_range_eq.hpp"

using namespace nativepg;

namespace {

// DATE
void test__parse_text_date__success()
{
    // Arrange
    types::pg_date d;
    std::string str = "1977-06-21";
    std::span<const unsigned char> data(reinterpret_cast<const unsigned char*>(str.data()), str.size());
    std::stringstream ss;

    // Act
    auto err = types::parse_text_date(data, d);

    // Assert
    BOOST_TEST_EQ(err, std::error_code());

    ss << std::format("{:%F}", d) << std::flush;
    BOOST_TEST_EQ(ss.str(), str);
}

void test__parse_binary_date__success()
{
    // Arrange
    types::pg_date d;
    std::string str = "1977-06-21";
    static constexpr unsigned char pg_date_big_endians[] = {0xFF, 0xFF, 0xDF, 0xDB};
    std::span<const unsigned char> data(pg_date_big_endians);
    std::stringstream ss;

    // Act
    auto err = types::parse_binary_date(data, d);

    // Assert
    BOOST_TEST_EQ(err, std::error_code());

    ss << std::format("{:%F}", d) << std::flush;
    BOOST_TEST_EQ(ss.str(), str);
}

// TIME
void test__parse_text_time__success()
{
    // Arrange
    std::chrono::microseconds us{};
    std::string str = "21:06:19";
    std::span<const unsigned char> data(reinterpret_cast<const unsigned char*>(str.data()), str.size());
    std::stringstream ss;

    // Act
    auto err = types::parse_text_time(data, us);

    // Assert
    BOOST_TEST_EQ(err, std::error_code());

    ss << std::format("{:%T}", std::chrono::duration_cast<std::chrono::seconds>(us)) << std::flush;
    BOOST_TEST_EQ(ss.str(), str);
}

void test__parse_binary_time__success()
{
    // Arrange
    std::chrono::microseconds us{};
    std::string str = "21:06:19";
    // 21:06:19 as bigendian microseconds data
    static constexpr unsigned char pg_time_210619[] = {0x00, 0x00, 0x00, 0x11, 0xB0, 0xB3, 0x88, 0xC0};
    std::span<const unsigned char> data(pg_time_210619);
    std::stringstream ss;

    // Act
    auto err = types::parse_binary_time(data, us);

    // Assert
    BOOST_TEST_EQ(err, std::error_code());

    ss << std::format("{:%T}", std::chrono::duration_cast<std::chrono::seconds>(us)) << std::flush;
    BOOST_TEST_EQ(ss.str(), str);
}

// TIMETZ
void test__parse_text_timetz__success()
{
    // Arrange
    types::pg_timetz tz{};
    std::string str = "21:06:19+07:00";
    std::span<const unsigned char> data(reinterpret_cast<const unsigned char*>(str.data()), str.size());
    std::stringstream ss;

    // Act
    auto err = types::parse_text_timetz(data, tz);

    // Assert
    BOOST_TEST_EQ(err, std::error_code());

    ss << std::format(
              "{0:%T}+{1:%H:%M}",
              std::chrono::duration_cast<std::chrono::seconds>(tz.time_since_midnight),
              std::chrono::duration_cast<std::chrono::minutes>(tz.utc_offset)
          )
       << std::flush;
    BOOST_TEST_EQ(ss.str(), str);
}

void test__parse_binary_timetz__success()
{
    // Arrange
    types::pg_timetz tz;
    std::string str = "12:34:23.435350+05:00";
    static constexpr unsigned char pg_time_be[] =
        {0x00, 0x00, 0x00, 0x0A, 0x89, 0xe9, 0x36, 0x56, 0xff, 0xff, 0xb9, 0xb0};
    std::span<const unsigned char> data(pg_time_be);
    std::stringstream ss;

    // Act
    auto err = types::parse_binary_timetz(data, tz);

    // Assert
    BOOST_TEST_EQ(err, std::error_code());

    ss << std::format("{0:%T}+{1:%H:%M}", tz.time_since_midnight, tz.utc_offset) << std::flush;
    BOOST_TEST_EQ(ss.str(), str);
}

// TIMESTAMP
void test__parse_text_timestamp__success()
{
    // Arrange
    types::pg_timestamp ts;
    std::string str = "1977-06-21 21:06:19";
    std::span<const unsigned char> data(reinterpret_cast<const unsigned char*>(str.data()), str.size());
    std::stringstream ss;

    // Act
    auto err = types::parse_text_timestamp(data, ts);

    // Assert
    BOOST_TEST_EQ(err, std::error_code());

    std::chrono::local_time<std::chrono::seconds> t_s = std::chrono::time_point_cast<std::chrono::seconds>(
        ts
    );
    ss << std::format("{0:%F} {1:%T}", ts, t_s) << std::flush;
    BOOST_TEST_EQ(ss.str(), str);
}

void test__parse_binary_timestamp__success()
{
    // Arrange
    types::pg_timestamp ts;
    std::string str = "2026-02-08 12:34:23.435350";
    static constexpr unsigned char pg_time_be[] = {0x00, 0x02, 0xed, 0x4E, 0x02, 0xc9, 0xd6, 0x56};
    std::span<const unsigned char> data(pg_time_be);
    std::stringstream ss;

    // Act
    auto err = types::parse_binary_timestamp(data, ts);

    // Assert
    BOOST_TEST_EQ(err, std::error_code());

    ss << std::format("{0:%F} {0:%H:%M:%S}", ts) << std::flush;
    BOOST_TEST_EQ(ss.str(), str);
}

// TIMESTAMPTZ
void test__parse_text_timestamptz__success()
{
    // Arrange
    types::pg_timestamptz ts;
    std::string str = "2026-02-08 20:03:00+00:00";
    std::span<const unsigned char> data(reinterpret_cast<const unsigned char*>(str.data()), str.size());
    std::stringstream ss;

    // Act
    auto err = types::parse_text_timestamptz(data, ts);

    // Assert
    BOOST_TEST_EQ(err, std::error_code());

    // We only check the year/month/day/hour/minute/second as timezone handling in format is tricky
    ss << std::format("{0:%F} {0:%H:%M:%S}", std::chrono::time_point_cast<std::chrono::seconds>(ts))
       << std::flush;
    BOOST_TEST_EQ(ss.str(), "2026-02-08 20:03:00");
}

void test__parse_binary_timestamptz__success()
{
    // Arrange
    types::pg_timestamptz ts;
    std::string str = "2026-02-08 12:34:23.435350";
    static constexpr unsigned char pg_time_be[] = {0x00, 0x02, 0xed, 0x4E, 0x02, 0xc9, 0xd6, 0x56};
    std::span<const unsigned char> data(pg_time_be);
    std::stringstream ss;

    // Act
    auto err = types::parse_binary_timestamptz(data, ts);

    // Assert
    BOOST_TEST_EQ(err, std::error_code());

    ss << std::format("{0:%F} {0:%H:%M:%S}", ts) << std::flush;
    BOOST_TEST_EQ(ss.str(), str);
}

// INTERVAL
void test__parse_text_interval__success()
{
    // Arrange
    types::pg_interval inv;
    std::string str = "1 year 2 mons 3 days 04:05:06.000007";
    std::span<const unsigned char> data(reinterpret_cast<const unsigned char*>(str.data()), str.size());

    // Act
    auto err = types::parse_text_interval(data, inv);

    // Assert
    BOOST_TEST_EQ(err, std::error_code());
    BOOST_TEST_EQ(inv.months, 14);
    BOOST_TEST_EQ(inv.days, 3);
    BOOST_TEST_EQ(
        inv.time.count(),
        (std::chrono::hours(4) + std::chrono::minutes(5) + std::chrono::seconds(6) +
         std::chrono::microseconds(7))
            .count()
    );
}

void test__parse_binary_interval__success()
{
    // Arrange
    types::pg_interval inv;
    // months: 1 = 0x00000001
    // days: 1 = 0x00000001
    // time: 1 microsecond = 0x0000000000000001
    static constexpr unsigned char pg_interval_be[] = {
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x01,  // time (8 bytes)
        0x00,
        0x00,
        0x00,
        0x01,  // days (4 bytes)
        0x00,
        0x00,
        0x00,
        0x01  // months (4 bytes)
    };
    std::span<const unsigned char> data(pg_interval_be);

    // Act
    auto err = types::parse_binary_interval(data, inv);

    // Assert
    BOOST_TEST_EQ(err, std::error_code());
    BOOST_TEST_EQ(inv.months, 1);
    BOOST_TEST_EQ(inv.days, 1);
    BOOST_TEST_EQ(inv.time.count(), 1);
}

// Serialization
using bytes = std::vector<unsigned char>;

template <class Fn>
bytes serialize_with(Fn fn)
{
    bytes to{0xde, 0xad};  // Verify that we append rather than overwrite
    BOOST_TEST_EQ(fn(to), std::error_code());
    BOOST_TEST(to.size() >= 2u);
    return bytes(to.begin() + 2, to.end());
}

std::string as_string(const bytes& b) { return std::string(b.begin(), b.end()); }

void check_bytes(
    const bytes& actual,
    const bytes& expected,
    std::source_location loc = std::source_location::current()
)
{
    test::test_range_eq(actual, expected, loc);
}

void test_serialize_date()
{
    using namespace std::chrono;
    const types::pg_date d1977{sys_days{year{1977} / 6 / 21}};
    const types::pg_date d2000{sys_days{year{2000} / 1 / 1}};
    const types::pg_date bc{sys_days{year{0} / 1 / 1}};

    BOOST_TEST_EQ(
        as_string(serialize_with([&](bytes& to) { return types::serialize_text_date(d1977, to); })),
        "1977-06-21"
    );
    BOOST_TEST_EQ(
        as_string(serialize_with([&](bytes& to) { return types::serialize_text_date(bc, to); })),
        "0001-01-01 BC"
    );
    check_bytes(
        serialize_with([&](bytes& to) { return types::serialize_binary_date(d1977, to); }),
        {0xff, 0xff, 0xdf, 0xdb}
    );
    check_bytes(
        serialize_with([&](bytes& to) { return types::serialize_binary_date(d2000, to); }),
        {0, 0, 0, 0}
    );

    // Infinities
    const auto inf = types::pg_date::max();
    const auto ninf = types::pg_date::min();
    BOOST_TEST_EQ(
        as_string(serialize_with([&](bytes& to) { return types::serialize_text_date(inf, to); })),
        "infinity"
    );
    BOOST_TEST_EQ(
        as_string(serialize_with([&](bytes& to) { return types::serialize_text_date(ninf, to); })),
        "-infinity"
    );
    check_bytes(
        serialize_with([&](bytes& to) { return types::serialize_binary_date(inf, to); }),
        {0x7f, 0xff, 0xff, 0xff}
    );
    check_bytes(
        serialize_with([&](bytes& to) { return types::serialize_binary_date(ninf, to); }),
        {0x80, 0, 0, 0}
    );
}

void test_serialize_date_out_of_range_error()
{
    const types::pg_date far{std::chrono::sys_days{std::chrono::days{2'000'000'000}}};
    bytes to;
    BOOST_TEST_EQ(types::serialize_text_date(far, to), std::error_code(client_errc::value_too_big));
    const types::pg_date too_early{std::chrono::sys_days{std::chrono::days{-2'147'483'648 + 100}}};
    BOOST_TEST_EQ(types::serialize_binary_date(too_early, to), std::error_code(client_errc::value_too_big));
    BOOST_TEST_EQ(to.size(), 0u);
}

void test_serialize_time()
{
    const types::pg_time t{49'530'500'000};
    BOOST_TEST_EQ(
        as_string(serialize_with([&](bytes& to) { return types::serialize_text_time(t, to); })),
        "13:45:30.500000"
    );
    BOOST_TEST_EQ(
        as_string(serialize_with([&](bytes& to) {
            return types::serialize_text_time(std::chrono::hours{24}, to);
        })),
        "24:00:00"
    );
    check_bytes(
        serialize_with([&](bytes& to) { return types::serialize_binary_time(t, to); }),
        {0x00, 0x00, 0x00, 0x0b, 0x88, 0x3f, 0x73, 0xa0}
    );

    // Out of range
    bytes to;
    BOOST_TEST_EQ(
        types::serialize_text_time(std::chrono::microseconds{-1}, to),
        std::error_code(client_errc::value_too_big)
    );
    BOOST_TEST_EQ(
        types::serialize_binary_time(std::chrono::hours{25}, to),
        std::error_code(client_errc::value_too_big)
    );
    BOOST_TEST_EQ(to.size(), 0u);
}

void test_serialize_timetz()
{
    const types::pg_timetz v{std::chrono::microseconds{49'530'500'000}, std::chrono::hours{2}};
    BOOST_TEST_EQ(
        as_string(serialize_with([&](bytes& to) { return types::serialize_text_timetz(v, to); })),
        "13:45:30.500000+02:00"
    );
    const types::pg_timetz neg{
        std::chrono::microseconds{0},
        -(std::chrono::hours{5} + std::chrono::minutes{30})
    };
    BOOST_TEST_EQ(
        as_string(serialize_with([&](bytes& to) { return types::serialize_text_timetz(neg, to); })),
        "00:00:00-05:30"
    );

    // The binary offset is seconds west of UTC
    check_bytes(
        serialize_with([&](bytes& to) { return types::serialize_binary_timetz(v, to); }),
        {0x00, 0x00, 0x00, 0x0b, 0x88, 0x3f, 0x73, 0xa0, 0xff, 0xff, 0xe3, 0xe0}
    );

    bytes to;
    const types::pg_timetz bad{std::chrono::microseconds{0}, std::chrono::hours{16}};
    BOOST_TEST_EQ(types::serialize_text_timetz(bad, to), std::error_code(client_errc::value_too_big));
    BOOST_TEST_EQ(types::serialize_binary_timetz(bad, to), std::error_code(client_errc::value_too_big));
    BOOST_TEST_EQ(to.size(), 0u);
}

void test_serialize_timestamp()
{
    using namespace std::chrono;
    const types::pg_timestamp ts{
        local_days{year{1977} / 6 / 21}.time_since_epoch() + microseconds{45'296'123'456}
    };
    BOOST_TEST_EQ(
        as_string(serialize_with([&](bytes& to) { return types::serialize_text_timestamp(ts, to); })),
        "1977-06-21 12:34:56.123456"
    );
    check_bytes(
        serialize_with([&](bytes& to) { return types::serialize_binary_timestamp(ts, to); }),
        {0xff, 0xfd, 0x79, 0x67, 0x4f, 0xbb, 0x1e, 0x40}
    );

    // The PostgreSQL epoch
    const types::pg_timestamp epoch{local_days{year{2000} / 1 / 1}.time_since_epoch()};
    check_bytes(
        serialize_with([&](bytes& to) { return types::serialize_binary_timestamp(epoch, to); }),
        {0, 0, 0, 0, 0, 0, 0, 0}
    );

    // Before the Unix epoch, with a fractional part
    const types::pg_timestamp before{
        local_days{year{1969} / 12 / 31}.time_since_epoch() + microseconds{86'399'000'001}
    };
    BOOST_TEST_EQ(
        as_string(serialize_with([&](bytes& to) { return types::serialize_text_timestamp(before, to); })),
        "1969-12-31 23:59:59.000001"
    );

    // BC
    const types::pg_timestamp bc{local_days{year{0} / 1 / 1}.time_since_epoch()};
    BOOST_TEST_EQ(
        as_string(serialize_with([&](bytes& to) { return types::serialize_text_timestamp(bc, to); })),
        "0001-01-01 00:00:00 BC"
    );

    // Infinities
    const auto inf = types::pg_timestamp::max();
    const auto ninf = types::pg_timestamp::min();
    BOOST_TEST_EQ(
        as_string(serialize_with([&](bytes& to) { return types::serialize_text_timestamp(inf, to); })),
        "infinity"
    );
    BOOST_TEST_EQ(
        as_string(serialize_with([&](bytes& to) { return types::serialize_text_timestamp(ninf, to); })),
        "-infinity"
    );
    check_bytes(
        serialize_with([&](bytes& to) { return types::serialize_binary_timestamp(inf, to); }),
        {0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}
    );
    check_bytes(
        serialize_with([&](bytes& to) { return types::serialize_binary_timestamp(ninf, to); }),
        {0x80, 0, 0, 0, 0, 0, 0, 0}
    );
}

void test_serialize_timestamptz()
{
    using namespace std::chrono;
    const types::pg_timestamptz ts{
        sys_days{year{1977} / 6 / 21}.time_since_epoch() + microseconds{45'296'123'456}
    };
    BOOST_TEST_EQ(
        as_string(serialize_with([&](bytes& to) { return types::serialize_text_timestamptz(ts, to); })),
        "1977-06-21 12:34:56.123456+00"
    );
    check_bytes(
        serialize_with([&](bytes& to) { return types::serialize_binary_timestamptz(ts, to); }),
        {0xff, 0xfd, 0x79, 0x67, 0x4f, 0xbb, 0x1e, 0x40}
    );

    const types::pg_timestamptz bc{sys_days{year{0} / 1 / 1}.time_since_epoch()};
    BOOST_TEST_EQ(
        as_string(serialize_with([&](bytes& to) { return types::serialize_text_timestamptz(bc, to); })),
        "0001-01-01 00:00:00+00 BC"
    );
}

void test_serialize_interval()
{
    using namespace std::chrono;
    const types::pg_interval iv{14, 3, hours{1} + minutes{2} + seconds{3} + microseconds{500'000}};
    BOOST_TEST_EQ(
        as_string(serialize_with([&](bytes& to) { return types::serialize_text_interval(iv, to); })),
        "14 mons 3 days 01:02:03.500000"
    );
    check_bytes(
        serialize_with([&](bytes& to) { return types::serialize_binary_interval(iv, to); }),
        {0x00, 0x00, 0x00, 0x00, 0xdd, 0xf0, 0x19, 0xe0, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x0e}
    );

    const types::pg_interval neg{-1, -2, -microseconds{1'500'000}};
    BOOST_TEST_EQ(
        as_string(serialize_with([&](bytes& to) { return types::serialize_text_interval(neg, to); })),
        "-1 mons -2 days -00:00:01.500000"
    );

    // Hours are not limited to 2 digits
    const types::pg_interval big{0, 0, hours{100}};
    BOOST_TEST_EQ(
        as_string(serialize_with([&](bytes& to) { return types::serialize_text_interval(big, to); })),
        "0 mons 0 days 100:00:00"
    );
}

void test_serialize_roundtrip_through_parse()
{
    using namespace std::chrono;
    const types::pg_timestamptz ts{
        sys_days{year{2024} / 2 / 29}.time_since_epoch() + microseconds{123'456'789}
    };
    for (bool binary : {false, true})
    {
        auto buff = serialize_with([&](bytes& to) {
            return binary ? types::serialize_binary_timestamptz(ts, to)
                          : types::serialize_text_timestamptz(ts, to);
        });
        types::pg_timestamptz out;
        auto ec = binary ? types::parse_binary_timestamptz(buff, out)
                         : types::parse_text_timestamptz(buff, out);
        BOOST_TEST_EQ(ec, std::error_code());
        BOOST_TEST(out == ts);
    }

    const types::pg_date d{sys_days{year{1999} / 12 / 31}};
    for (bool binary : {false, true})
    {
        auto buff = serialize_with([&](bytes& to) {
            return binary ? types::serialize_binary_date(d, to) : types::serialize_text_date(d, to);
        });
        types::pg_date out;
        auto ec = binary ? types::parse_binary_date(buff, out) : types::parse_text_date(buff, out);
        BOOST_TEST_EQ(ec, std::error_code());
        BOOST_TEST(out == d);
    }
}

static_assert(serializable_field<std::chrono::sys_days>);
static_assert(serializable_field<std::chrono::microseconds>);
static_assert(serializable_field<types::pg_timetz>);
static_assert(serializable_field<types::pg_timestamp>);
static_assert(serializable_field<types::pg_timestamptz>);
static_assert(serializable_field<types::pg_interval>);
static_assert(field_serialize_oid<std::chrono::sys_days> == 1082);
static_assert(field_serialize_oid<std::chrono::microseconds> == 1083);
static_assert(field_serialize_oid<types::pg_timetz> == 1266);
static_assert(field_serialize_oid<types::pg_timestamp> == 1114);
static_assert(field_serialize_oid<types::pg_timestamptz> == 1184);
static_assert(field_serialize_oid<types::pg_interval> == 1186);

void test_field_serialize_dispatch()
{
    const types::pg_date d{std::chrono::sys_days{std::chrono::year{1977} / 6 / 21}};
    bytes text, binary;
    BOOST_TEST_EQ(field_serialize(d, protocol::format_code::text, text), std::error_code());
    BOOST_TEST_EQ(field_serialize(d, protocol::format_code::binary, binary), std::error_code());
    BOOST_TEST_EQ(as_string(text), "1977-06-21");
    check_bytes(binary, {0xff, 0xff, 0xdf, 0xdb});
}

}  // namespace

// TODO: traits tests missing

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

    test_serialize_date();
    test_serialize_date_out_of_range_error();
    test_serialize_time();
    test_serialize_timetz();
    test_serialize_timestamp();
    test_serialize_timestamptz();
    test_serialize_interval();
    test_serialize_roundtrip_through_parse();
    test_field_serialize_dispatch();

    return boost::report_errors();
}
