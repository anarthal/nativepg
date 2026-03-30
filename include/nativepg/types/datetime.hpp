
#ifndef NATIVEPG_TYPES_DATETIME_HPP
#define NATIVEPG_TYPES_DATETIME_HPP

#include <boost/endian/conversion.hpp>
#include <boost/system/error_code.hpp>

#include <charconv>
#include <chrono>
#include <cctype>
#include <cstring>
#include <span>
#include <string_view>
#include <system_error>

#include "nativepg/client_errc.hpp"

namespace nativepg::types {

using boost::system::error_code;


/*
| Type        | Category | OID  | Storage size | Precision | Minimum value                  | Maximum value                  |
|-------------|----------|------|--------------|-----------|--------------------------------|--------------------------------|
| date        | base     | 1082 | 4 bytes      | 1 day     | 4713-01-01 BC                  | 5874897-12-31                  |
| time        | base     | 1083 | 8 bytes      | 1 µs      | 00:00:00                       | 24:00:00                       |
| timetz      | base     | 1266 | 12 bytes     | 1 µs      | 00:00:00-15:59                 | 24:00:00+15:59                 |
| timestamp   | base     | 1114 | 8 bytes      | 1 µs      | 4713-01-01 00:00:00 BC         | 294276-12-31 23:59:59.999999   |
| timestamptz | base     | 1184 | 8 bytes      | 1 µs      | 4713-01-01 00:00:00+00 BC      | 294276-12-31 23:59:59.999999+00|
| interval    | base     | 1186 | 16 bytes     | 1 µs      | -178000000 years               | 178000000 years                |
| tsrange     | range    | 3908 | variable     | 1 µs      | timestamp min                  | timestamp max                  |
| tstzrange   | range    | 3910 | variable     | 1 µs      | timestamptz min                | timestamptz max                |
| daterange   | range    | 3912 | variable     | 1 day     | date min                       | date max                       |
 */

// Type mapping
using pg_date = std::chrono::sys_days;

using pg_time = std::chrono::microseconds;
struct pg_timetz {
    std::chrono::microseconds time_since_midnight;
    std::chrono::seconds utc_offset;
};

using pg_timestamp = std::chrono::local_time<std::chrono::microseconds>;
using pg_timestamptz = std::chrono::sys_time<std::chrono::microseconds>;

struct pg_interval {
    int months;
    int days;
    std::chrono::microseconds time;
};

namespace detail {

inline std::string_view trim(std::string_view sv)
{
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front())))
        sv.remove_prefix(1);
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back())))
        sv.remove_suffix(1);
    return sv;
}

inline bool iequals(std::string_view a, std::string_view b)
{
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        auto ca = static_cast<unsigned char>(a[i]);
        auto cb = static_cast<unsigned char>(b[i]);
        if (std::tolower(ca) != std::tolower(cb))
            return false;
    }
    return true;
}

inline bool consume_bc(std::string_view& sv)
{
    sv = trim(sv);
    if (sv.size() < 2)
        return false;
    std::string_view tail = sv.substr(sv.size() - 2);
    if (!iequals(tail, "BC"))
        return false;
    sv.remove_suffix(2);
    sv = trim(sv);
    return true;
}

template <class T>
inline bool parse_infinity(std::string_view sv, T& to)
{
    sv = trim(sv);
    if (iequals(sv, "infinity"))
    {
        to = T::max();
        return true;
    }
    if (iequals(sv, "-infinity"))
    {
        to = T::min();
        return true;
    }
    return false;
}

inline error_code parse_date_parts(std::string_view sv, int& year, int& month, int& day) noexcept
{
    sv = trim(sv);
    if (sv.empty())
        return client_errc::protocol_value_error;

    const char* first = sv.data();
    const char* last = first + sv.size();
    const char* pos = first;

    auto res = std::from_chars(pos, last, year);
    if (res.ec != std::errc{})
        return std::make_error_code(res.ec);
    pos = res.ptr;
    if (pos >= last || *pos != '-')
        return client_errc::protocol_value_error;
    ++pos;

    res = std::from_chars(pos, last, month);
    if (res.ec != std::errc{})
        return std::make_error_code(res.ec);
    pos = res.ptr;
    if (pos >= last || *pos != '-')
        return client_errc::protocol_value_error;
    ++pos;

    res = std::from_chars(pos, last, day);
    if (res.ec != std::errc{})
        return std::make_error_code(res.ec);
    pos = res.ptr;
    if (pos != last)
        return client_errc::protocol_value_error;

    return {};
}

inline error_code parse_time_prefix(
    std::string_view sv,
    std::size_t& pos,
    std::chrono::microseconds& out
) noexcept
{
    const char* first = sv.data();
    const char* last = first + sv.size();
    const char* p = first + pos;

    int hours{};
    auto res = std::from_chars(p, last, hours);
    if (res.ec != std::errc{})
        return std::make_error_code(res.ec);
    p = res.ptr;
    if (p >= last || *p != ':')
        return client_errc::protocol_value_error;
    ++p;

    int minutes{};
    res = std::from_chars(p, last, minutes);
    if (res.ec != std::errc{})
        return std::make_error_code(res.ec);
    p = res.ptr;
    if (p >= last || *p != ':')
        return client_errc::protocol_value_error;
    ++p;

    int seconds{};
    res = std::from_chars(p, last, seconds);
    if (res.ec != std::errc{})
        return std::make_error_code(res.ec);
    p = res.ptr;

    int us = 0;
    if (p < last && *p == '.')
    {
        ++p;
        const char* start = p;
        while (p < last && std::isdigit(static_cast<unsigned char>(*p)))
            ++p;
        if (start == p)
            return client_errc::protocol_value_error;
        int fraction = 0;
        res = std::from_chars(start, p, fraction);
        if (res.ec != std::errc{})
            return std::make_error_code(res.ec);
        int n = p - start;
        if (n > 6) {
            for (int i = 0; i < n - 6; ++i) fraction /= 10;
        } else {
            for (int i = 0; i < 6 - n; ++i) fraction *= 10;
        }
        us = fraction;
    }

    if (hours < 0 || minutes < 0 || seconds < 0)
        return client_errc::protocol_value_error;
    if (minutes > 59 || seconds > 59)
        return client_errc::protocol_value_error;
    if (hours > 24)
        return client_errc::protocol_value_error;
    if (hours == 24 && (minutes != 0 || seconds != 0 || us != 0))
        return client_errc::protocol_value_error;

    out = std::chrono::hours{hours} + std::chrono::minutes{minutes} +
          std::chrono::seconds{seconds} + std::chrono::microseconds{us};
    pos = static_cast<std::size_t>(p - first);
    return {};
}

inline error_code parse_tz_suffix(
    std::string_view sv,
    std::size_t pos,
    std::chrono::seconds& offset
) noexcept
{
    sv = trim(sv.substr(pos));
    if (sv.empty())
    {
        offset = std::chrono::seconds{0};
        return {};
    }

    if (iequals(sv, "Z") || iequals(sv, "UTC") || iequals(sv, "UT") || iequals(sv, "GMT"))
    {
        offset = std::chrono::seconds{0};
        return {};
    }

    const char* p = sv.data();
    const char* last = p + sv.size();
    if (*p != '+' && *p != '-')
        return client_errc::protocol_value_error;
    int sign = *p == '+' ? 1 : -1;
    ++p;

    int hours = 0;
    int minutes = 0;
    int digits = 0;
    while (p < last && std::isdigit(static_cast<unsigned char>(*p)) && digits < 2)
    {
        hours = hours * 10 + (*p - '0');
        ++p;
        ++digits;
    }
    if (digits == 0)
        return client_errc::protocol_value_error;

    if (p == last)
    {
        minutes = 0;
    }
    else if (*p == ':')
    {
        ++p;
        if (last - p < 2)
            return client_errc::protocol_value_error;
        if (!std::isdigit(static_cast<unsigned char>(p[0])) ||
            !std::isdigit(static_cast<unsigned char>(p[1])))
            return client_errc::protocol_value_error;
        minutes = (p[0] - '0') * 10 + (p[1] - '0');
        p += 2;
    }
    else if (std::isdigit(static_cast<unsigned char>(*p)))
    {
        // HHMM format
        if (last - p != 2)
            return client_errc::protocol_value_error;
        if (!std::isdigit(static_cast<unsigned char>(p[0])) ||
            !std::isdigit(static_cast<unsigned char>(p[1])))
            return client_errc::protocol_value_error;
        minutes = (p[0] - '0') * 10 + (p[1] - '0');
        p += 2;
    }
    else
    {
        return client_errc::protocol_value_error;
    }

    if (p != last)
        return client_errc::protocol_value_error;
    if (hours < 0 || hours > 15 || minutes < 0 || minutes > 59)
        return client_errc::protocol_value_error;

    offset = (std::chrono::hours{hours} + std::chrono::minutes{minutes}) * sign;
    return {};
}

}  // namespace detail



// DATE => std::chrono::sys_days. (TEXT)
template <class T = std::chrono::sys_days>
constexpr error_code parse_text_date(std::span<const unsigned char> from, T& to) noexcept
{
    std::string_view sv{reinterpret_cast<const char*>(from.data()), from.size()};
    if (detail::parse_infinity(sv, to))
        return {};

    bool bc = detail::consume_bc(sv);
    int year{}, month{}, day{};
    auto ec = detail::parse_date_parts(sv, year, month, day);
    if (ec)
        return ec;

    if (bc)
        year = 1 - year;

    std::chrono::year y{year};
    std::chrono::month m{static_cast<unsigned>(month)};
    std::chrono::day d{static_cast<unsigned>(day)};
    std::chrono::year_month_day ymd{y, m, d};
    if (!ymd.ok())
        return client_errc::protocol_value_error;

    to = std::chrono::sys_days{ymd};
    return {};
}

// DATE => std::chrono::sys_days. (BINARY)
template <class T = std::chrono::sys_days>
constexpr error_code parse_binary_date(std::span<const unsigned char> from, T& to) noexcept
{
    if (from.size() != 4)
        return client_errc::protocol_value_error;

    // Load big-endian int32 directly
    int32_t days_since_2000 = boost::endian::endian_load<int32_t, 4, boost::endian::order::big>(from.data());

    // PostgreSQL zero = 2000-01-01
    constexpr std::chrono::sys_days pg_epoch{std::chrono::year{2000}/1/1};

    to = pg_epoch + std::chrono::days{days_since_2000};

    return error_code{};
}

// TIME => std::chrono::microseconds (TEXT)
template <class T = std::chrono::microseconds>
constexpr error_code parse_text_time(std::span<const unsigned char> from, T& to) noexcept
{
    if (from.empty())
        return client_errc::protocol_value_error;
    std::string_view sv{reinterpret_cast<const char*>(from.data()), from.size()};
    sv = detail::trim(sv);
    std::size_t pos = 0;
    std::chrono::microseconds out;
    auto ec = detail::parse_time_prefix(sv, pos, out);
    if (ec)
        return ec;
    if (detail::trim(sv.substr(pos)).size() != 0)
        return client_errc::protocol_value_error;
    to = out;
    return {};
}

// TIME => std::chrono::microseconds (BINARY).
template <class T = std::chrono::microseconds>
constexpr error_code parse_binary_time(std::span<const unsigned char> from, T& to) noexcept
{
    if (from.size() != 8)
        return client_errc::protocol_value_error;

    auto us = boost::endian::endian_load<int64_t, sizeof(int64_t), boost::endian::order::big>(from.data());

    to = std::chrono::microseconds{us};

    return error_code{};
}



// TIMETZ => pg_timetz (TEXT)
template <class T = types::pg_timetz>
constexpr error_code parse_text_timetz(std::span<const unsigned char> from, T& to) noexcept
{
    std::string_view sv{reinterpret_cast<const char*>(from.data()), from.size()};
    sv = detail::trim(sv);
    std::size_t pos = 0;
    std::chrono::microseconds time_of_day;
    auto ec = detail::parse_time_prefix(sv, pos, time_of_day);
    if (ec)
        return ec;

    std::chrono::seconds offset{0};
    ec = detail::parse_tz_suffix(sv, pos, offset);
    if (ec)
        return ec;

    to = T{time_of_day, offset};
    return {};
}

// TIMETZ => pg_timetz (BINARY)
template <class T = types::pg_timetz>
constexpr error_code parse_binary_timetz(std::span<const unsigned char> from, T& to) noexcept
{
    if (from.size() != 12) // 8 + 4 bytes
        return client_errc::protocol_value_error;

    const unsigned char* ptr = from.data();

    // --- Load time (int64) big endian ---
    std::int64_t time_us =
        boost::endian::endian_load<std::int64_t, 8, boost::endian::order::big>(ptr);

    // --- Load UTC offset (int32) big endian ---
    std::int32_t offset_west_s =
        boost::endian::endian_load<std::int32_t, 4, boost::endian::order::big>(ptr + 8);

    to.time_since_midnight = std::chrono::microseconds{time_us};

    // PostgreSQL stores seconds WEST of UTC → negate
    to.utc_offset = std::chrono::seconds{-offset_west_s};

    return error_code{};
}

// TIMESTAMP => pg_timestamp / std::chrono::local_time<microseconds> (TEXT)
template <class T = types::pg_timestamp>
constexpr error_code parse_text_timestamp(std::span<const unsigned char> from, T& to) noexcept
{
    std::string_view sv{reinterpret_cast<const char*>(from.data()), from.size()};
    if (detail::parse_infinity(sv, to))
        return {};

    bool bc = detail::consume_bc(sv);
    sv = detail::trim(sv);
    auto sep = sv.find_first_of(" T");
    if (sep == std::string_view::npos)
        return client_errc::protocol_value_error;

    std::string_view date_sv = sv.substr(0, sep);
    std::string_view time_sv = detail::trim(sv.substr(sep + 1));

    int year{}, month{}, day{};
    auto ec = detail::parse_date_parts(date_sv, year, month, day);
    if (ec)
        return ec;
    if (bc)
        year = 1 - year;

    std::chrono::year y{year};
    std::chrono::month m{static_cast<unsigned>(month)};
    std::chrono::day d{static_cast<unsigned>(day)};
    std::chrono::year_month_day ymd{y, m, d};
    if (!ymd.ok())
        return client_errc::protocol_value_error;

    std::size_t pos = 0;
    std::chrono::microseconds tod;
    ec = detail::parse_time_prefix(time_sv, pos, tod);
    if (ec)
        return ec;
    if (detail::trim(time_sv.substr(pos)).size() != 0)
        return client_errc::protocol_value_error;

    to = T{std::chrono::sys_days{ymd}.time_since_epoch() + tod};
    return {};
}

// TIMESTAMP => pg_timestamp (BINARY)
template <class T = types::pg_timestamp>
constexpr error_code parse_binary_timestamp(
    std::span<const unsigned char> from, T& to) noexcept
{
    if (from.size() != 8)
        return client_errc::protocol_value_error;

    // Load big-endian int64: microseconds since 2000-01-01
    std::int64_t us_since_2000 =
        boost::endian::endian_load<std::int64_t, 8, boost::endian::order::big>(
            from.data());

    // PostgreSQL epoch
    constexpr std::chrono::sys_days pg_epoch{
        std::chrono::year{2000}/1/1
    };

    // Cast sys_days → microseconds duration
    auto us_since_epoch = std::chrono::time_point_cast<std::chrono::microseconds>(pg_epoch)
                          .time_since_epoch()
                        + std::chrono::microseconds{us_since_2000};

    to = T{us_since_epoch};

    return {};
}

// TIMESTAMPTZ => pg_timestamptz (TEXT)
template <class T = types::pg_timestamptz>
constexpr error_code parse_text_timestamptz(std::span<const unsigned char> from, T& to) noexcept
{
    std::string_view sv{reinterpret_cast<const char*>(from.data()), from.size()};
    if (detail::parse_infinity(sv, to))
        return {};

    bool bc = detail::consume_bc(sv);
    sv = detail::trim(sv);
    auto sep = sv.find_first_of(" T");
    if (sep == std::string_view::npos)
        return client_errc::protocol_value_error;

    std::string_view date_sv = sv.substr(0, sep);
    std::string_view time_sv = detail::trim(sv.substr(sep + 1));

    int year{}, month{}, day{};
    auto ec = detail::parse_date_parts(date_sv, year, month, day);
    if (ec)
        return ec;
    if (bc)
        year = 1 - year;

    std::chrono::year y{year};
    std::chrono::month m{static_cast<unsigned>(month)};
    std::chrono::day d{static_cast<unsigned>(day)};
    std::chrono::year_month_day ymd{y, m, d};
    if (!ymd.ok())
        return client_errc::protocol_value_error;

    std::size_t pos = 0;
    std::chrono::microseconds tod;
    ec = detail::parse_time_prefix(time_sv, pos, tod);
    if (ec)
        return ec;

    std::chrono::seconds offset{0};
    ec = detail::parse_tz_suffix(time_sv, pos, offset);
    if (ec)
        return ec;

    to = T{std::chrono::sys_days{ymd}.time_since_epoch() + tod -
           std::chrono::duration_cast<std::chrono::microseconds>(offset)};
    return {};
}

// TIMESTAMPTZ => pg_timestamp (BINARY)
template <class T = types::pg_timestamptz>
constexpr error_code parse_binary_timestamptz(
    std::span<const unsigned char> from, T& to) noexcept
{
    if (from.size() != 8)
        return client_errc::protocol_value_error;

    // Load big-endian int64: microseconds since 2000-01-01
    std::int64_t us_since_2000 =
        boost::endian::endian_load<std::int64_t, 8, boost::endian::order::big>(
            from.data());

    // PostgreSQL epoch
    constexpr std::chrono::sys_days pg_epoch{
        std::chrono::year{2000}/1/1
    };

    // Cast sys_days → microseconds duration
    auto us_since_epoch = std::chrono::time_point_cast<std::chrono::microseconds>(pg_epoch)
                          .time_since_epoch()
                        + std::chrono::microseconds{us_since_2000};

    to = T{us_since_epoch};

    return {};
}


// INTERVAL  => pg_interval (TEXT)
template <class T = types::pg_interval>
constexpr error_code parse_text_interval(std::span<const unsigned char> from, T& to) noexcept
{
    using namespace std::chrono;

    to = {}; // zero everything

    if (from.empty()) return client_errc::protocol_value_error;

    std::string_view sv{reinterpret_cast<const char*>(from.data()), from.size()};
    size_t pos = 0;

    auto skip_ws = [&]() {
        while (pos < sv.size() && std::isspace(static_cast<unsigned char>(sv[pos]))) ++pos;
    };

    auto parse_int = [&](int& val) -> bool {
        skip_ws();
        if (pos >= sv.size()) return false;
        int sign = 1;
        if (sv[pos] == '-') { sign = -1; ++pos; }
        else if (sv[pos] == '+') { ++pos; }

        const char* start = sv.data() + pos;
        const char* end = sv.data() + sv.size();
        auto res = std::from_chars(start, end, val);
        if (res.ec != std::errc{}) return false;
        pos += res.ptr - start;
        val *= sign;
        return true;
    };

    while (pos < sv.size()) {
        skip_ws();
        if (pos >= sv.size()) break;

        // Check for time part HH:MM:SS (may have a sign)
        // A time part MUST have a colon at some point before the next space or end of string.
        size_t next_ws = sv.find_first_of(" \t\n\r", pos);
        std::string_view part = sv.substr(pos, next_ws == std::string_view::npos ? std::string_view::npos : next_ws - pos);

        if (part.find(':') != std::string_view::npos) {
            int sign = 1;
            bool has_sign = false;
            if (sv[pos] == '-') { sign = -1; ++pos; has_sign = true; }
            else if (sv[pos] == '+') { ++pos; has_sign = true; }

            microseconds time_part;
            // The time part is what remains in 'part' after potentially skipping the sign
            size_t sign_offset = has_sign ? 1 : 0;
            auto ec = parse_text_time(
                std::span{reinterpret_cast<const unsigned char*>(sv.data() + pos), part.size() - sign_offset},
                time_part
            );
            if (ec) return ec;

            to.time += time_part * sign;
            pos += part.size() - sign_offset;
        } else {
            // Otherwise parse "value unit"
            int val = 0;
            if (!parse_int(val)) return client_errc::protocol_value_error;

            skip_ws();
            size_t unit_start = pos;
            while (pos < sv.size() && std::isalpha(static_cast<unsigned char>(sv[pos]))) ++pos;
            std::string_view unit = sv.substr(unit_start, pos - unit_start);

            if (unit == "year" || unit == "years") to.months += val * 12;
            else if (unit == "mon" || unit == "mons") to.months += val;
            else if (unit == "day" || unit == "days") to.days += val;
            else if (unit == "hour" || unit == "hours") to.time += hours{val};
            else if (unit == "minute" || unit == "minutes") to.time += minutes{val};
            else if (unit == "second" || unit == "seconds") to.time += seconds{val};
            else return client_errc::protocol_value_error;
        }
        skip_ws();
    }

    return {};
}

// INTERVAL => pg_interval (BINARY)
template <class T = types::pg_interval>
constexpr error_code parse_binary_interval(
    std::span<const unsigned char> from, T& to) noexcept
{
    if (from.size() != 16)
        return client_errc::protocol_value_error;

    // PostgreSQL binary interval:
    // 8 bytes: time (microseconds, int64)
    // 4 bytes: days (int32)
    // 4 bytes: months (int32)
    to.time = std::chrono::microseconds{
        boost::endian::endian_load<std::int64_t, 8, boost::endian::order::big>(from.data())
    };
    to.days = boost::endian::endian_load<std::int32_t, 4, boost::endian::order::big>(from.data() + 8);
    to.months = boost::endian::endian_load<std::int32_t, 4, boost::endian::order::big>(from.data() + 12);

    return {};
}


}

#endif  // NATIVEPG_TYPES_DATETIME_HPP
