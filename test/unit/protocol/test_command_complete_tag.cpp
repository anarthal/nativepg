//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/core/lightweight_test.hpp>
#include <boost/system/error_code.hpp>

#include <cstdint>
#include <limits>
#include <optional>
#include <ostream>
#include <string_view>
#include <system_error>
#include <vector>

#include "nativepg/client_errc.hpp"
#include "nativepg/protocol/command_complete_tag.hpp"

using namespace nativepg;
using boost::system::error_code;
using protocol::parse_command_complete_tag;

namespace {

// A string that is always on the heap and allocates exactly the number of bytes that the string has.
// Helps sanitizers detect invalid memory reads
struct heap_string
{
    std::vector<char> data;

    explicit heap_string(std::string_view sv) : data(sv.begin(), sv.end()) {}

    operator std::string_view() const { return {data.data(), data.size()}; }
};

template <class T>
struct printable_opt
{
    const std::optional<T>& value;

    friend bool operator==(const printable_opt& self, const T& rhs) { return self.value == rhs; }

    friend std::ostream& operator<<(std::ostream& os, printable_opt<T> value)
    {
        if (value.value.has_value())
            return os << value.value.value();
        return os << "<nullopt>";
    }
};

void test_success()
{
    {
        // INSERT
        std::optional<std::uint64_t> rows;
        BOOST_TEST_EQ(parse_command_complete_tag(heap_string{"INSERT 0 2"}, rows), error_code());
        BOOST_TEST_EQ(printable_opt{rows}, 2u);
    }
    {
        // INSERT, rows u64 max
        std::optional<std::uint64_t> rows;
        BOOST_TEST_EQ(
            parse_command_complete_tag(heap_string{"INSERT 0 18446744073709551615"}, rows),
            error_code()
        );
        BOOST_TEST_EQ(printable_opt{rows}, (std::numeric_limits<std::uint64_t>::max)());
    }
    {
        // INSERT, oid non-zero (deprecated, but we should be able to parse it)
        std::optional<std::uint64_t> rows;
        BOOST_TEST_EQ(parse_command_complete_tag(heap_string{"INSERT 42 2"}, rows), error_code());
        BOOST_TEST_EQ(printable_opt{rows}, 2u);
    }
    {
        // INSERT, zero affected rows
        std::optional<std::uint64_t> rows;
        BOOST_TEST_EQ(parse_command_complete_tag(heap_string{"INSERT 0 0"}, rows), error_code());
        BOOST_TEST_EQ(printable_opt{rows}, 0u);
    }
    {
        // INSERT, zero affected rows
        std::optional<std::uint64_t> rows;
        BOOST_TEST_EQ(parse_command_complete_tag(heap_string{"INSERT 0 0"}, rows), error_code());
        BOOST_TEST_EQ(printable_opt{rows}, 0u);
    }
    {
        // INSERT, OID is not a number (we discard the OID so we tolerate this)
        std::optional<std::uint64_t> rows;
        BOOST_TEST_EQ(parse_command_complete_tag(heap_string{"INSERT bad 2"}, rows), error_code());
        BOOST_TEST_EQ(printable_opt{rows}, 2u);
    }
    {
        // INSERT, OID is empty (we discard the OID so we tolerate this)
        std::optional<std::uint64_t> rows;
        BOOST_TEST_EQ(parse_command_complete_tag(heap_string{"INSERT  4"}, rows), error_code());
        BOOST_TEST_EQ(printable_opt{rows}, 4u);
    }
    {
        // SELECT, row count > 0
        std::optional<std::uint64_t> rows;
        BOOST_TEST_EQ(parse_command_complete_tag(heap_string{"SELECT 10"}, rows), error_code());
        BOOST_TEST_EQ(printable_opt{rows}, 10u);
    }
    {
        // SELECT, row count = 0
        std::optional<std::uint64_t> rows;
        BOOST_TEST_EQ(parse_command_complete_tag(heap_string{"SELECT 0"}, rows), error_code());
        BOOST_TEST_EQ(printable_opt{rows}, 0u);
    }
    {
        // SELECT, row count = u64 max
        std::optional<std::uint64_t> rows;
        BOOST_TEST_EQ(
            parse_command_complete_tag(heap_string{"SELECT 18446744073709551615"}, rows),
            error_code()
        );
        BOOST_TEST_EQ(printable_opt{rows}, (std::numeric_limits<std::uint64_t>::max)());
    }
    {
        // DELETE
        std::optional<std::uint64_t> rows;
        BOOST_TEST_EQ(parse_command_complete_tag(heap_string{"DELETE 3"}, rows), error_code());
        BOOST_TEST_EQ(printable_opt{rows}, 3u);
    }
    {
        // UPDATE
        std::optional<std::uint64_t> rows;
        BOOST_TEST_EQ(parse_command_complete_tag(heap_string{"UPDATE 5"}, rows), error_code());
        BOOST_TEST_EQ(printable_opt{rows}, 5u);
    }
    {
        // FETCH
        std::optional<std::uint64_t> rows;
        BOOST_TEST_EQ(parse_command_complete_tag(heap_string{"FETCH 7"}, rows), error_code());
        BOOST_TEST_EQ(printable_opt{rows}, 7u);
    }
    {
        // MERGE
        std::optional<std::uint64_t> rows;
        BOOST_TEST_EQ(parse_command_complete_tag(heap_string{"MERGE 9"}, rows), error_code());
        BOOST_TEST_EQ(printable_opt{rows}, 9u);
    }
    {
        // MOVE
        std::optional<std::uint64_t> rows;
        BOOST_TEST_EQ(parse_command_complete_tag(heap_string{"MOVE 11"}, rows), error_code());
        BOOST_TEST_EQ(printable_opt{rows}, 11u);
    }
    {
        // COPY
        std::optional<std::uint64_t> rows;
        BOOST_TEST_EQ(parse_command_complete_tag(heap_string{"COPY 13"}, rows), error_code());
        BOOST_TEST_EQ(printable_opt{rows}, 13u);
    }
    {
        // Truncate table (no row count): the tag carries no affected-rows info
        std::optional<std::uint64_t> rows;
        BOOST_TEST_EQ(parse_command_complete_tag(heap_string{"TRUNCATE TABLE"}, rows), error_code());
        BOOST_TEST_NOT(rows.has_value());
    }
}

void test_error()
{
    {
        // INSERT, rows not a number
        std::optional<std::uint64_t> rows{42};
        BOOST_TEST_EQ(
            parse_command_complete_tag(heap_string{"INSERT 0 bad"}, rows),
            std::make_error_code(std::errc::invalid_argument)
        );
        BOOST_TEST_EQ(printable_opt{rows}, 42);  // unmodified
    }
    {
        // INSERT, rows <0
        std::optional<std::uint64_t> rows{42};
        BOOST_TEST_EQ(
            parse_command_complete_tag(heap_string{"INSERT 0 -1"}, rows),
            std::make_error_code(std::errc::invalid_argument)
        );
        BOOST_TEST_EQ(printable_opt{rows}, 42);  // unmodified
    }
    {
        // INSERT, rows > u64 max
        std::optional<std::uint64_t> rows{42};
        BOOST_TEST_EQ(
            parse_command_complete_tag(heap_string{"INSERT 0 18446744073709551616"}, rows),
            std::make_error_code(std::errc::result_out_of_range)
        );
        BOOST_TEST_EQ(printable_opt{rows}, 42);  // unmodified
    }
    {
        // INSERT, extra bytes
        std::optional<std::uint64_t> rows{42};
        BOOST_TEST_EQ(
            parse_command_complete_tag(heap_string{"INSERT 0 2 bad"}, rows),
            error_code(client_errc::extra_bytes)
        );
        BOOST_TEST_EQ(printable_opt{rows}, 42);  // unmodified
    }
    {
        // INSERT, rows hex, with leading 0x
        std::optional<std::uint64_t> rows{42};
        BOOST_TEST_EQ(
            parse_command_complete_tag(heap_string{"INSERT 0 0xff"}, rows),
            error_code(client_errc::extra_bytes)
        );
        BOOST_TEST_EQ(printable_opt{rows}, 42);  // unmodified
    }
    {
        // INSERT, rows hex, without leading 0x
        std::optional<std::uint64_t> rows{42};
        BOOST_TEST_EQ(
            parse_command_complete_tag(heap_string{"INSERT 0 ff"}, rows),
            std::make_error_code(std::errc::invalid_argument)
        );
        BOOST_TEST_EQ(printable_opt{rows}, 42);  // unmodified
    }
    {
        // INSERT, EOF after space but before rows
        std::optional<std::uint64_t> rows{42};
        BOOST_TEST_EQ(
            parse_command_complete_tag(heap_string{"INSERT 0 "}, rows),
            std::make_error_code(std::errc::invalid_argument)
        );
        BOOST_TEST_EQ(printable_opt{rows}, 42);  // unmodified
    }
    {
        // INSERT, only one space between INSERT and row count, no OID: the row count is
        // consumed as the OID, and we hit EOF looking for the space before the row count
        std::optional<std::uint64_t> rows{42};
        BOOST_TEST_EQ(
            parse_command_complete_tag(heap_string{"INSERT 5"}, rows),
            error_code(client_errc::incomplete_message)
        );
        BOOST_TEST_EQ(printable_opt{rows}, 42);  // unmodified
    }
    {
        // INSERT, two spaces between OID and row count: from_chars chokes on the leading space
        std::optional<std::uint64_t> rows{42};
        BOOST_TEST_EQ(
            parse_command_complete_tag(heap_string{"INSERT 0  2"}, rows),
            std::make_error_code(std::errc::invalid_argument)
        );
        BOOST_TEST_EQ(printable_opt{rows}, 42);  // unmodified
    }
    {
        // INSERT, two spaces between INSERT and OID: the OID is empty, the row count is
        // consumed as the OID, leaving trailing bytes
        std::optional<std::uint64_t> rows{42};
        BOOST_TEST_EQ(
            parse_command_complete_tag(heap_string{"INSERT  0 2"}, rows),
            error_code(client_errc::extra_bytes)
        );
        BOOST_TEST_EQ(printable_opt{rows}, 42);  // unmodified
    }
    {
        // INSERT, no space between INSERT and OID
        std::optional<std::uint64_t> rows{42};
        BOOST_TEST_EQ(
            parse_command_complete_tag(heap_string{"INSERT0 2"}, rows),
            error_code(client_errc::protocol_value_error)
        );
        BOOST_TEST_EQ(printable_opt{rows}, 42);  // unmodified
    }
    {
        // INSERT, no OID or row count
        std::optional<std::uint64_t> rows{42};
        BOOST_TEST_EQ(
            parse_command_complete_tag(heap_string{"INSERT"}, rows),
            error_code(client_errc::incomplete_message)
        );
        BOOST_TEST_EQ(printable_opt{rows}, 42);  // unmodified
    }
}

}  // namespace

int main()
{
    test_success();
    test_error();

    return boost::report_errors();
}
