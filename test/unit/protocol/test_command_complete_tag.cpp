//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/core/lightweight_test.hpp>
#include <boost/system/error_code.hpp>

#include <cstdint>
#include <optional>
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

void test_insert_success()
{
    {
        heap_string str{"INSERT 0 2"};
        std::optional<std::uint64_t> rows;
        BOOST_TEST_EQ(parse_command_complete_tag(str, rows), error_code());
        BOOST_TEST_EQ(rows.value(), 2u);
    }
}

void test_insert_error()
{
    {
        heap_string str{"INSERT 0 bad"};
        std::optional<std::uint64_t> rows;
        BOOST_TEST_EQ(
            parse_command_complete_tag(str, rows),
            std::make_error_code(std::errc::invalid_argument)
        );
        BOOST_TEST_NOT(rows.has_value());
    }
}

}  // namespace

int main()
{
    test_insert_success();
    test_insert_error();

    return boost::report_errors();
}
