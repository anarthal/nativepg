//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/core/lightweight_test.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "nativepg/client_errc.hpp"
#include "nativepg/field_traits.hpp"
#include "nativepg/protocol/common.hpp"
#include "nativepg/types/array.hpp"

using namespace nativepg;
using protocol::format_code;

namespace {

constexpr std::int32_t int4_array_oid = 1007;
constexpr std::int32_t text_array_oid = 1009;

field_view fv(std::string_view s)
{
    return field_view(
        std::span<const unsigned char>(reinterpret_cast<const unsigned char*>(s.data()), s.size())
    );
}

std::string_view sv(const std::vector<unsigned char>& v)
{
    return {reinterpret_cast<const char*>(v.data()), v.size()};
}

std::string bin(std::initializer_list<int> bytes)
{
    std::string r;
    for (int b : bytes)
        r.push_back(static_cast<char>(b));
    return r;
}

void test_oid_mapping()
{
    BOOST_TEST_EQ(types::array_oid_for_element(23), 1007);
    BOOST_TEST_EQ(types::element_oid_for_array(1009), 25);
    BOOST_TEST_EQ(types::array_oid_for_element(9999), 0);
    BOOST_TEST_EQ(types::element_oid_for_array(9999), 0);
    BOOST_TEST_EQ(field_serialize_oid<std::vector<std::int32_t>>, 1007);
    BOOST_TEST_EQ(field_serialize_oid<std::span<const std::string>>, 1009);
}

void test_is_compatible()
{
    BOOST_TEST_EQ(field_is_compatible<std::vector<std::int32_t>>(int4_array_oid), std::error_code());
    BOOST_TEST_EQ(
        field_is_compatible<std::vector<std::int32_t>>(text_array_oid),
        std::error_code(client_errc::incompatible_field_type)
    );
    BOOST_TEST_EQ(
        field_is_compatible<std::vector<std::int32_t>>(23),
        std::error_code(client_errc::incompatible_field_type)
    );
}

void test_parse_text_ints()
{
    std::vector<std::int32_t> v{99};
    BOOST_TEST_EQ(field_parse(fv("{1,2,-3}"), int4_array_oid, format_code::text, v), std::error_code());
    const std::vector<std::int32_t> expected{1, 2, -3};
    BOOST_TEST_ALL_EQ(v.begin(), v.end(), expected.begin(), expected.end());
}

void test_parse_text_empty()
{
    std::vector<std::int32_t> v{99};
    BOOST_TEST_EQ(field_parse(fv("{}"), int4_array_oid, format_code::text, v), std::error_code());
    BOOST_TEST(v.empty());
}

void test_parse_text_strings()
{
    std::vector<std::string> v;
    BOOST_TEST_EQ(
        field_parse(fv(R"({abc,"a b","q\"x\\",""})"), text_array_oid, format_code::text, v),
        std::error_code()
    );
    const std::vector<std::string> expected{"abc", "a b", "q\"x\\", ""};
    BOOST_TEST_ALL_EQ(v.begin(), v.end(), expected.begin(), expected.end());
}

void test_parse_text_nulls()
{
    std::vector<std::optional<std::int32_t>> v;
    BOOST_TEST_EQ(field_parse(fv("{1,NULL,3}"), int4_array_oid, format_code::text, v), std::error_code());
    BOOST_TEST_EQ(v.size(), 3u);
    BOOST_TEST(v[0] == 1);
    BOOST_TEST(!v[1].has_value());
    BOOST_TEST(v[2] == 3);

    // A quoted "NULL" is a string
    std::vector<std::optional<std::string>> s;
    BOOST_TEST_EQ(
        field_parse(fv(R"({"NULL",NULL})"), text_array_oid, format_code::text, s),
        std::error_code()
    );
    BOOST_TEST(s[0] == "NULL");
    BOOST_TEST(!s[1].has_value());
}

void test_parse_text_null_in_non_optional()
{
    std::vector<std::int32_t> v;
    BOOST_TEST_EQ(
        field_parse(fv("{1,NULL}"), int4_array_oid, format_code::text, v),
        std::error_code(client_errc::unexpected_null)
    );
}

void test_parse_text_errors()
{
    std::vector<std::int32_t> v;
    for (auto bad : {"", "1,2", "{1,2", "{1,2}x", "{{1},{2}}", "{1,,2}", "{1 2}", "{\"1}"})
        BOOST_TEST(field_parse(fv(bad), int4_array_oid, format_code::text, v));
    BOOST_TEST_EQ(
        field_parse(field_view(), int4_array_oid, format_code::text, v),
        std::error_code(client_errc::unexpected_null)
    );
}

void test_parse_text_dimension_decoration()
{
    std::vector<std::int32_t> v;
    BOOST_TEST_EQ(field_parse(fv("[2:3]={4,5}"), int4_array_oid, format_code::text, v), std::error_code());
    BOOST_TEST_EQ(v.size(), 2u);
}

void test_parse_binary()
{
    const auto data = bin({0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 23, 0, 0, 0, 2, 0, 0,
                           0, 1, 0, 0, 0, 4, 0, 0, 0, 7, 0, 0,  0, 4, 0, 0, 0, 8});
    std::vector<std::int32_t> v;
    BOOST_TEST_EQ(field_parse(fv(data), int4_array_oid, format_code::binary, v), std::error_code());
    const std::vector<std::int32_t> expected{7, 8};
    BOOST_TEST_ALL_EQ(v.begin(), v.end(), expected.begin(), expected.end());
}

void test_parse_binary_empty_and_null_element()
{
    std::vector<std::int32_t> v{1};
    BOOST_TEST_EQ(
        field_parse(fv(bin({0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 23})), int4_array_oid, format_code::binary, v),
        std::error_code()
    );
    BOOST_TEST(v.empty());

    const auto data = bin({0, 0, 0, 1, 0, 0, 0, 1, 0,    0,    0,    23,
                           0, 0, 0, 1, 0, 0, 0, 1, 0xff, 0xff, 0xff, 0xff});
    std::vector<std::optional<std::int32_t>> o;
    BOOST_TEST_EQ(field_parse(fv(data), int4_array_oid, format_code::binary, o), std::error_code());
    BOOST_TEST_EQ(o.size(), 1u);
    BOOST_TEST(!o[0].has_value());
}

void test_parse_binary_errors()
{
    std::vector<std::int32_t> v;
    // Too short
    BOOST_TEST_EQ(
        field_parse(fv(bin({0, 0, 0, 1})), int4_array_oid, format_code::binary, v),
        std::error_code(client_errc::incomplete_message)
    );
    // Two dimensions
    BOOST_TEST(
        field_parse(fv(bin({0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 23})), int4_array_oid, format_code::binary, v)
    );
    // Wrong element type
    BOOST_TEST(
        field_parse(fv(bin({0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 25})), int4_array_oid, format_code::binary, v)
    );
    // Huge element count
    BOOST_TEST(field_parse(
        fv(bin({0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 23, 0x7f, 0xff, 0xff, 0xff, 0, 0, 0, 1})),
        int4_array_oid,
        format_code::binary,
        v
    ));
    // Truncated element
    BOOST_TEST_EQ(
        field_parse(
            fv(bin({0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 23, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 4, 0, 0})),
            int4_array_oid,
            format_code::binary,
            v
        ),
        std::error_code(client_errc::incomplete_message)
    );
    // Trailing bytes
    BOOST_TEST_EQ(
        field_parse(
            fv(bin({0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 23, 0})),
            int4_array_oid,
            format_code::binary,
            v
        ),
        std::error_code(client_errc::extra_bytes)
    );
}

void test_serialize_text()
{
    std::vector<unsigned char> buf;
    BOOST_TEST_EQ(
        field_serialize(std::vector<std::int32_t>{1, -2, 3}, format_code::text, buf),
        std::error_code()
    );
    BOOST_TEST_EQ(sv(buf), "{1,-2,3}");

    buf.clear();
    BOOST_TEST_EQ(field_serialize(std::vector<std::int32_t>{}, format_code::text, buf), std::error_code());
    BOOST_TEST_EQ(sv(buf), "{}");

    buf.clear();
    const std::vector<std::optional<std::string>>
        strs{"abc", "a b", "q\"x\\", "", "null", std::nullopt, "{}"};
    BOOST_TEST_EQ(field_serialize(strs, format_code::text, buf), std::error_code());
    BOOST_TEST_EQ(sv(buf), R"({abc,"a b","q\"x\\","","null",NULL,"{}"})");
}

void test_serialize_binary()
{
    std::vector<unsigned char> buf;
    const std::vector<std::int32_t> v{7, 8};
    BOOST_TEST_EQ(field_serialize(v, format_code::binary, buf), std::error_code());
    const auto expected = bin({0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 23, 0, 0, 0, 2, 0, 0,
                               0, 1, 0, 0, 0, 4, 0, 0, 0, 7, 0, 0,  0, 4, 0, 0, 0, 8});
    BOOST_TEST_EQ(sv(buf), expected);

    buf.clear();
    BOOST_TEST_EQ(field_serialize(std::vector<std::int32_t>{}, format_code::binary, buf), std::error_code());
    BOOST_TEST_EQ(sv(buf), bin({0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 23}));

    buf.clear();
    const std::vector<std::optional<std::int32_t>> o{std::nullopt};
    BOOST_TEST_EQ(field_serialize(o, format_code::binary, buf), std::error_code());
    BOOST_TEST_EQ(sv(buf), bin({0, 0, 0, 1, 0, 0, 0, 1, 0,    0,    0,    23,
                                0, 0, 0, 1, 0, 0, 0, 1, 0xff, 0xff, 0xff, 0xff}));
}

void test_serialize_span()
{
    const std::vector<std::int32_t> v{1, 2};
    std::vector<unsigned char> buf{'x'};
    BOOST_TEST_EQ(
        field_serialize(std::span<const std::int32_t>(v), format_code::text, buf),
        std::error_code()
    );
    BOOST_TEST_EQ(sv(buf), "x{1,2}");
}

void test_roundtrip()
{
    const std::vector<std::optional<std::string>> in{"a,b", std::nullopt, "", "\"q\"", "plain"};
    for (auto code : {format_code::text, format_code::binary})
    {
        std::vector<unsigned char> buf;
        BOOST_TEST_EQ(field_serialize(in, code, buf), std::error_code());
        std::vector<std::optional<std::string>> out;
        BOOST_TEST_EQ(
            field_parse(field_view(std::span<const unsigned char>(buf)), text_array_oid, code, out),
            std::error_code()
        );
        BOOST_TEST(in == out);
    }
}

}  // namespace

int main()
{
    test_oid_mapping();
    test_is_compatible();
    test_parse_text_ints();
    test_parse_text_empty();
    test_parse_text_strings();
    test_parse_text_nulls();
    test_parse_text_null_in_non_optional();
    test_parse_text_errors();
    test_parse_text_dimension_decoration();
    test_parse_binary();
    test_parse_binary_empty_and_null_element();
    test_parse_binary_errors();
    test_serialize_text();
    test_serialize_binary();
    test_serialize_span();
    test_roundtrip();

    return boost::report_errors();
}
