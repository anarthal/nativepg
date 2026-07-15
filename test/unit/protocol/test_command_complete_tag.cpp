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
        // Insert
        std::optional<std::uint64_t> rows;
        BOOST_TEST_EQ(parse_command_complete_tag(heap_string{"INSERT 0 2"}, rows), error_code());
        BOOST_TEST_EQ(printable_opt{rows}, 2u);
    }
    {
        // Insert rows u64 max
        std::optional<std::uint64_t> rows;
        BOOST_TEST_EQ(
            parse_command_complete_tag(heap_string{"INSERT 0 18446744073709551615"}, rows),
            error_code()
        );
        BOOST_TEST_EQ(printable_opt{rows}, (std::numeric_limits<std::uint64_t>::max)());
    }
    {
        // Insert with oid non-zero (deprecated, but we should be able to parse it)
        std::optional<std::uint64_t> rows;
        BOOST_TEST_EQ(parse_command_complete_tag(heap_string{"INSERT 42 2"}, rows), error_code());
        BOOST_TEST_EQ(printable_opt{rows}, 2u);
    }
    {
        // Insert with zero affected rows
        std::optional<std::uint64_t> rows;
        BOOST_TEST_EQ(parse_command_complete_tag(heap_string{"INSERT 0 0"}, rows), error_code());
        BOOST_TEST_EQ(printable_opt{rows}, 0u);
    }
    {
        // Select some rows
        std::optional<std::uint64_t> rows;
        BOOST_TEST_EQ(parse_command_complete_tag(heap_string{"SELECT 10"}, rows), error_code());
        BOOST_TEST_EQ(printable_opt{rows}, 10u);
    }
    {
        // Select zero rows
        std::optional<std::uint64_t> rows;
        BOOST_TEST_EQ(parse_command_complete_tag(heap_string{"SELECT 0"}, rows), error_code());
        BOOST_TEST_EQ(printable_opt{rows}, 0u);
    }
    {
        // Select u64 max
        std::optional<std::uint64_t> rows;
        BOOST_TEST_EQ(
            parse_command_complete_tag(heap_string{"SELECT 18446744073709551615"}, rows),
            error_code()
        );
        BOOST_TEST_EQ(printable_opt{rows}, (std::numeric_limits<std::uint64_t>::max)());
    }
    {
        // Delete some rows
        std::optional<std::uint64_t> rows;
        BOOST_TEST_EQ(parse_command_complete_tag(heap_string{"DELETE 3"}, rows), error_code());
        BOOST_TEST_EQ(printable_opt{rows}, 3u);
    }
    {
        // Update some rows
        std::optional<std::uint64_t> rows;
        BOOST_TEST_EQ(parse_command_complete_tag(heap_string{"UPDATE 5"}, rows), error_code());
        BOOST_TEST_EQ(printable_opt{rows}, 5u);
    }
    {
        // Fetch some rows
        std::optional<std::uint64_t> rows;
        BOOST_TEST_EQ(parse_command_complete_tag(heap_string{"FETCH 7"}, rows), error_code());
        BOOST_TEST_EQ(printable_opt{rows}, 7u);
    }
    {
        // Merge some rows
        std::optional<std::uint64_t> rows;
        BOOST_TEST_EQ(parse_command_complete_tag(heap_string{"MERGE 9"}, rows), error_code());
        BOOST_TEST_EQ(printable_opt{rows}, 9u);
    }
    {
        // Move some rows
        std::optional<std::uint64_t> rows;
        BOOST_TEST_EQ(parse_command_complete_tag(heap_string{"MOVE 11"}, rows), error_code());
        BOOST_TEST_EQ(printable_opt{rows}, 11u);
    }
    {
        // Copy some rows
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

    // Insert OID is not a number
    // Insert OID is empty
}

void test_error()
{
    {
        // Insert rows not a number
        std::optional<std::uint64_t> rows{42};
        BOOST_TEST_EQ(
            parse_command_complete_tag(heap_string{"INSERT 0 bad"}, rows),
            std::make_error_code(std::errc::invalid_argument)
        );
        BOOST_TEST_EQ(printable_opt{rows}, 42);  // unmodified
    }

    // Insert rows <0
    // Insert rows > u64 max
    // Insert rows hex
    // Insert EOF after space but before rows
    // Insert only one space between INSERT and row count, no OID
    // Insert two spaces between OID and row count
    // Insert two spaces between INSERT and OID
    // Insert no space between INSERT and OID
    // Insert alone

    // Value left as is
}

}  // namespace

int main()
{
    test_success();
    test_error();

    return boost::report_errors();
}
