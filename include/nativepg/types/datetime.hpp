
#ifndef NATIVEPG_TYPES_DATETIME_HPP
#define NATIVEPG_TYPES_DATETIME_HPP

#include <boost/endian/conversion.hpp>

#include <cctype>
#include <charconv>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>
#include <system_error>
#include <vector>

#include "nativepg/client_errc.hpp"

namespace nativepg::types {



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

inline std::error_code parse_date_parts(std::string_view sv, int& year, int& month, int& day) noexcept
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

inline std::error_code parse_time_prefix(
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

inline std::error_code parse_tz_suffix(
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
std::error_code parse_text_date(std::span<const unsigned char> from, T& to) noexcept
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
std::error_code parse_binary_date(std::span<const unsigned char> from, T& to) noexcept
{
    if (from.size() != 4)
        return client_errc::protocol_value_error;

    // Load big-endian int32 directly
    int32_t days_since_2000 = boost::endian::endian_load<int32_t, 4, boost::endian::order::big>(from.data());

    // PostgreSQL zero = 2000-01-01
    constexpr std::chrono::sys_days pg_epoch{std::chrono::year{2000}/1/1};

    to = pg_epoch + std::chrono::days{days_since_2000};

    return {};
}

// TIME => std::chrono::microseconds (TEXT)
template <class T = std::chrono::microseconds>
std::error_code parse_text_time(std::span<const unsigned char> from, T& to) noexcept
{
    if (from.empty())
        return client_errc::protocol_value_error;
    std::string_view sv{reinterpret_cast<const char*>(from.data()), from.size()};
    sv = detail::trim(sv);
    std::size_t pos = 0;
    std::chrono::microseconds out{};
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
std::error_code parse_binary_time(std::span<const unsigned char> from, T& to) noexcept
{
    if (from.size() != 8)
        return client_errc::protocol_value_error;

    auto us = boost::endian::endian_load<int64_t, sizeof(int64_t), boost::endian::order::big>(from.data());

    to = std::chrono::microseconds{us};

    return {};
}



// TIMETZ => pg_timetz (TEXT)
template <class T = types::pg_timetz>
std::error_code parse_text_timetz(std::span<const unsigned char> from, T& to) noexcept
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
std::error_code parse_binary_timetz(std::span<const unsigned char> from, T& to) noexcept
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

    return {};
}

// TIMESTAMP => pg_timestamp / std::chrono::local_time<microseconds> (TEXT)
template <class T = types::pg_timestamp>
std::error_code parse_text_timestamp(std::span<const unsigned char> from, T& to) noexcept
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
std::error_code parse_binary_timestamp(
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
std::error_code parse_text_timestamptz(std::span<const unsigned char> from, T& to) noexcept
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
std::error_code parse_binary_timestamptz(
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
std::error_code parse_text_interval(std::span<const unsigned char> from, T& to) noexcept
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

            microseconds time_part {};
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
std::error_code parse_binary_interval(
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

// --- Serialization

namespace detail {

inline constexpr std::int64_t micros_per_day = 86'400'000'000;
inline constexpr std::int64_t micros_per_second = 1'000'000;

// Days between the Unix epoch (used by std::chrono) and the PostgreSQL epoch (2000-01-01)
inline constexpr std::int64_t pg_epoch_days = 10'957;
inline constexpr std::int64_t pg_epoch_micros = pg_epoch_days * micros_per_day;

// Day counts beyond this can't be converted to a std::chrono::year_month_day safely
inline constexpr std::int64_t max_calendar_days = 12'000'000;

// Offsets accepted by PostgreSQL for timetz: up to 15:59:59 in either direction
inline constexpr std::int64_t max_utc_offset_seconds = 15 * 3600 + 59 * 60 + 59;

// Appends value, left-padded with zeros to at least width digits
template <std::integral T>
void append_padded(std::vector<unsigned char>& to, T value, std::size_t width)
{
    char buffer[24];
    const auto res = std::to_chars(buffer, buffer + sizeof(buffer), value);
    const auto len = static_cast<std::size_t>(res.ptr - buffer);
    if (len < width)
        to.insert(to.end(), width - len, static_cast<unsigned char>('0'));
    to.insert(to.end(), buffer, res.ptr);
}

inline void append_chars(std::vector<unsigned char>& to, std::string_view sv)
{
    to.insert(to.end(), sv.begin(), sv.end());
}

// Appends YYYY-MM-DD. Returns true if the date is BC, in which case the caller must append " BC"
inline bool append_date_text(std::vector<unsigned char>& to, const std::chrono::year_month_day& ymd)
{
    const int year = static_cast<int>(ymd.year());
    const bool bc = year <= 0;
    append_padded(to, bc ? 1 - year : year, 4);
    to.push_back('-');
    append_padded(to, static_cast<unsigned>(ymd.month()), 2);
    to.push_back('-');
    append_padded(to, static_cast<unsigned>(ymd.day()), 2);
    return bc;
}

// Appends HH:MM:SS[.ffffff]. The hours field is not limited to 2 digits
inline void append_time_text(std::vector<unsigned char>& to, std::uint64_t micros)
{
    const std::uint64_t total_seconds = micros / micros_per_second;
    const std::uint64_t fraction = micros % micros_per_second;
    append_padded(to, total_seconds / 3600, 2);
    to.push_back(':');
    append_padded(to, total_seconds / 60 % 60, 2);
    to.push_back(':');
    append_padded(to, total_seconds % 60, 2);
    if (fraction != 0)
    {
        to.push_back('.');
        append_padded(to, fraction, 6);
    }
}

inline std::error_code check_time_of_day(std::int64_t micros)
{
    return micros < 0 || micros > micros_per_day ? std::error_code(client_errc::value_too_big)
                                                 : std::error_code();
}

// Splits a number of microseconds since the Unix epoch into a calendar date and a time of day
inline std::error_code split_timestamp(
    std::int64_t micros,
    std::chrono::year_month_day& ymd,
    std::int64_t& time_of_day
)
{
    std::int64_t days = micros / micros_per_day;
    time_of_day = micros % micros_per_day;
    if (time_of_day < 0)
    {
        time_of_day += micros_per_day;
        --days;
    }
    if (days > max_calendar_days || days < -max_calendar_days)
        return client_errc::value_too_big;

    ymd = std::chrono::year_month_day{
        std::chrono::sys_days{std::chrono::days(static_cast<std::chrono::days::rep>(days))}
    };
    return ymd.ok() ? std::error_code() : std::error_code(client_errc::value_too_big);
}

template <class T>
std::error_code serialize_text_timestamp_impl(
    const T& value,
    bool with_utc_suffix,
    std::vector<unsigned char>& to
)
{
    if (value == T::max())
    {
        append_chars(to, "infinity");
        return {};
    }
    if (value == T::min())
    {
        append_chars(to, "-infinity");
        return {};
    }

    std::chrono::year_month_day ymd;
    std::int64_t time_of_day{};
    if (auto ec = split_timestamp(
            static_cast<std::int64_t>(value.time_since_epoch().count()),
            ymd,
            time_of_day
        ))
        return ec;

    const bool bc = append_date_text(to, ymd);
    to.push_back(' ');
    append_time_text(to, static_cast<std::uint64_t>(time_of_day));
    if (with_utc_suffix)
        append_chars(to, "+00");
    if (bc)
        append_chars(to, " BC");
    return {};
}

template <class T>
std::error_code serialize_binary_timestamp_impl(const T& value, std::vector<unsigned char>& to)
{
    constexpr auto min = (std::numeric_limits<std::int64_t>::min)();
    constexpr auto max = (std::numeric_limits<std::int64_t>::max)();

    // The infinities are encoded as the extreme values of the 64-bit integer
    std::int64_t result{};
    if (value == T::max())
    {
        result = max;
    }
    else if (value == T::min())
    {
        result = min;
    }
    else
    {
        const auto micros = static_cast<std::int64_t>(value.time_since_epoch().count());
        if (micros < min + 1 + pg_epoch_micros)
            return client_errc::value_too_big;
        result = micros - pg_epoch_micros;
    }

    const auto offset = to.size();
    to.resize(offset + 8);
    boost::endian::endian_store<std::int64_t, 8, boost::endian::order::big>(to.data() + offset, result);
    return {};
}

inline std::error_code append_utc_offset_text(std::vector<unsigned char>& to, std::chrono::seconds offset)
{
    const std::int64_t secs = offset.count();
    if (secs > max_utc_offset_seconds || secs < -max_utc_offset_seconds)
        return client_errc::value_too_big;
    to.push_back(secs < 0 ? '-' : '+');
    const auto mag = static_cast<std::uint64_t>(secs < 0 ? -secs : secs);
    append_padded(to, mag / 3600, 2);
    to.push_back(':');
    append_padded(to, mag / 60 % 60, 2);
    if (mag % 60 != 0)
    {
        to.push_back(':');
        append_padded(to, mag % 60, 2);
    }
    return {};
}

}  // namespace detail

// DATE (TEXT). The infinities are the extreme values of std::chrono::sys_days
template <class T = std::chrono::sys_days>
std::error_code serialize_text_date(const T& value, std::vector<unsigned char>& to)
{
    if (value == T::max())
    {
        detail::append_chars(to, "infinity");
        return {};
    }
    if (value == T::min())
    {
        detail::append_chars(to, "-infinity");
        return {};
    }

    const auto days = static_cast<std::int64_t>(value.time_since_epoch().count());
    if (days > detail::max_calendar_days || days < -detail::max_calendar_days)
        return client_errc::value_too_big;
    const std::chrono::year_month_day ymd{
        std::chrono::sys_days{std::chrono::days(static_cast<std::chrono::days::rep>(days))}
    };
    if (!ymd.ok())
        return client_errc::value_too_big;

    if (detail::append_date_text(to, ymd))
        detail::append_chars(to, " BC");
    return {};
}

// DATE (BINARY)
template <class T = std::chrono::sys_days>
std::error_code serialize_binary_date(const T& value, std::vector<unsigned char>& to)
{
    constexpr auto min = (std::numeric_limits<std::int32_t>::min)();
    constexpr auto max = (std::numeric_limits<std::int32_t>::max)();

    std::int32_t result{};
    if (value == T::max())
    {
        result = max;
    }
    else if (value == T::min())
    {
        result = min;
    }
    else
    {
        const auto days = static_cast<std::int64_t>(value.time_since_epoch().count()) - detail::pg_epoch_days;
        if (days <= min || days >= max)
            return client_errc::value_too_big;
        result = static_cast<std::int32_t>(days);
    }

    const auto offset = to.size();
    to.resize(offset + 4);
    boost::endian::endian_store<std::int32_t, 4, boost::endian::order::big>(to.data() + offset, result);
    return {};
}

// TIME (TEXT)
inline std::error_code serialize_text_time(std::chrono::microseconds value, std::vector<unsigned char>& to)
{
    const auto micros = static_cast<std::int64_t>(value.count());
    if (auto ec = detail::check_time_of_day(micros))
        return ec;
    detail::append_time_text(to, static_cast<std::uint64_t>(micros));
    return {};
}

// TIME (BINARY)
inline std::error_code serialize_binary_time(std::chrono::microseconds value, std::vector<unsigned char>& to)
{
    const auto micros = static_cast<std::int64_t>(value.count());
    if (auto ec = detail::check_time_of_day(micros))
        return ec;
    const auto offset = to.size();
    to.resize(offset + 8);
    boost::endian::endian_store<std::int64_t, 8, boost::endian::order::big>(to.data() + offset, micros);
    return {};
}

// TIMETZ (TEXT)
template <class T = types::pg_timetz>
std::error_code serialize_text_timetz(const T& value, std::vector<unsigned char>& to)
{
    const auto micros = static_cast<std::int64_t>(value.time_since_midnight.count());
    if (auto ec = detail::check_time_of_day(micros))
        return ec;

    // Validate before appending anything, so that errors leave the buffer untouched
    const std::int64_t secs = value.utc_offset.count();
    if (secs > detail::max_utc_offset_seconds || secs < -detail::max_utc_offset_seconds)
        return client_errc::value_too_big;

    detail::append_time_text(to, static_cast<std::uint64_t>(micros));
    return detail::append_utc_offset_text(to, value.utc_offset);
}

// TIMETZ (BINARY). PostgreSQL stores the offset as seconds WEST of UTC
template <class T = types::pg_timetz>
std::error_code serialize_binary_timetz(const T& value, std::vector<unsigned char>& to)
{
    const auto micros = static_cast<std::int64_t>(value.time_since_midnight.count());
    if (auto ec = detail::check_time_of_day(micros))
        return ec;
    const std::int64_t secs = value.utc_offset.count();
    if (secs > detail::max_utc_offset_seconds || secs < -detail::max_utc_offset_seconds)
        return client_errc::value_too_big;

    const auto offset = to.size();
    to.resize(offset + 12);
    boost::endian::endian_store<std::int64_t, 8, boost::endian::order::big>(to.data() + offset, micros);
    boost::endian::endian_store<std::int32_t, 4, boost::endian::order::big>(
        to.data() + offset + 8,
        static_cast<std::int32_t>(-secs)
    );
    return {};
}

// TIMESTAMP (TEXT)
template <class T = types::pg_timestamp>
std::error_code serialize_text_timestamp(const T& value, std::vector<unsigned char>& to)
{
    return detail::serialize_text_timestamp_impl(value, false, to);
}

// TIMESTAMP (BINARY)
template <class T = types::pg_timestamp>
std::error_code serialize_binary_timestamp(const T& value, std::vector<unsigned char>& to)
{
    return detail::serialize_binary_timestamp_impl(value, to);
}

// TIMESTAMPTZ (TEXT). Always sent as UTC
template <class T = types::pg_timestamptz>
std::error_code serialize_text_timestamptz(const T& value, std::vector<unsigned char>& to)
{
    return detail::serialize_text_timestamp_impl(value, true, to);
}

// TIMESTAMPTZ (BINARY)
template <class T = types::pg_timestamptz>
std::error_code serialize_binary_timestamptz(const T& value, std::vector<unsigned char>& to)
{
    return detail::serialize_binary_timestamp_impl(value, to);
}

// INTERVAL (TEXT), in PostgreSQL's "postgres" style: "<months> mons <days> days [-]HH:MM:SS[.ffffff]"
template <class T = types::pg_interval>
std::error_code serialize_text_interval(const T& value, std::vector<unsigned char>& to)
{
    detail::append_padded(to, value.months, 0);
    detail::append_chars(to, " mons ");
    detail::append_padded(to, value.days, 0);
    detail::append_chars(to, " days ");

    const auto micros = static_cast<std::int64_t>(value.time.count());
    if (micros < 0)
        to.push_back('-');
    // Negating in unsigned arithmetic is well defined, even for the minimum value
    const auto magnitude = micros < 0 ? std::uint64_t{0} - static_cast<std::uint64_t>(micros)
                                      : static_cast<std::uint64_t>(micros);
    detail::append_time_text(to, magnitude);
    return {};
}

// INTERVAL (BINARY)
template <class T = types::pg_interval>
std::error_code serialize_binary_interval(const T& value, std::vector<unsigned char>& to)
{
    const auto offset = to.size();
    to.resize(offset + 16);
    boost::endian::endian_store<std::int64_t, 8, boost::endian::order::big>(
        to.data() + offset,
        static_cast<std::int64_t>(value.time.count())
    );
    boost::endian::endian_store<std::int32_t, 4, boost::endian::order::big>(
        to.data() + offset + 8,
        static_cast<std::int32_t>(value.days)
    );
    boost::endian::endian_store<std::int32_t, 4, boost::endian::order::big>(
        to.data() + offset + 12,
        static_cast<std::int32_t>(value.months)
    );
    return {};
}

}  // namespace nativepg::types

#endif  // NATIVEPG_TYPES_DATETIME_HPP
