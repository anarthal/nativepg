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
#include "nativepg/types/multi_array.hpp"

using namespace nativepg;
using protocol::format_code;
namespace multi = boost::multi;

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

// {{1,2,3},{4,5,6}}
multi::array<std::int32_t, 2> make_2x3()
{
    multi::array<std::int32_t, 2> a({2, 3}, 0);
    std::int32_t n = 1;
    for (auto& v : a.elements())
        v = n++;
    return a;
}

void check_2x3(const multi::array<std::int32_t, 2>& a)
{
    BOOST_TEST_EQ(a.num_elements(), 6);
    BOOST_TEST_EQ(a.size(), 2);
    BOOST_TEST_EQ(a[0].size(), 3);
    for (std::int32_t i = 0; i < 2; ++i)
        for (std::int32_t j = 0; j < 3; ++j)
            BOOST_TEST_EQ(a[i][j], i * 3 + j + 1);
}

void test_is_compatible()
{
    using arr = multi::array<std::int32_t, 2>;
    BOOST_TEST_EQ(field_is_compatible<arr>(int4_array_oid), std::error_code());
    BOOST_TEST_EQ(
        field_is_compatible<arr>(text_array_oid),
        std::error_code(client_errc::incompatible_field_type)
    );
    BOOST_TEST_EQ(field_is_compatible<arr>(23), std::error_code(client_errc::incompatible_field_type));
    BOOST_TEST_EQ(field_serialize_oid<arr>, 1007);
}

void test_parse_text_2d()
{
    multi::array<std::int32_t, 2> a;
    BOOST_TEST_EQ(
        field_parse(fv("{{1,2,3},{4,5,6}}"), int4_array_oid, format_code::text, a),
        std::error_code()
    );
    check_2x3(a);
}

void test_parse_text_3d()
{
    multi::array<std::int32_t, 3> a;
    BOOST_TEST_EQ(
        field_parse(
            fv("{{{1,2},{3,4}},{{5,6},{7,8}},{{9,10},{11,12}}}"),
            int4_array_oid,
            format_code::text,
            a
        ),
        std::error_code()
    );
    BOOST_TEST_EQ(a.size(), 3);
    BOOST_TEST_EQ(a[0].size(), 2);
    BOOST_TEST_EQ(a[0][0].size(), 2);
    BOOST_TEST_EQ(a[2][1][0], 11);
    BOOST_TEST_EQ(a[1][0][1], 6);
}

void test_parse_text_1d()
{
    multi::array<std::int32_t, 1> a;
    BOOST_TEST_EQ(field_parse(fv("{4,5,6}"), int4_array_oid, format_code::text, a), std::error_code());
    BOOST_TEST_EQ(a.size(), 3);
    BOOST_TEST_EQ(a[2], 6);
}

void test_parse_text_strings_and_nulls()
{
    multi::array<std::optional<std::string>, 2> a;
    BOOST_TEST_EQ(
        field_parse(fv(R"({{"a b",NULL},{"{x}","q\"z"}})"), text_array_oid, format_code::text, a),
        std::error_code()
    );
    BOOST_TEST(a[0][0] == "a b");
    BOOST_TEST(!a[0][1].has_value());
    BOOST_TEST(a[1][0] == "{x}");
    BOOST_TEST(a[1][1] == "q\"z");
}

void test_parse_text_empty()
{
    multi::array<std::int32_t, 2> a = make_2x3();
    BOOST_TEST_EQ(field_parse(fv("{}"), int4_array_oid, format_code::text, a), std::error_code());
    BOOST_TEST_EQ(a.num_elements(), 0);
}

void test_parse_text_errors()
{
    multi::array<std::int32_t, 2> a = make_2x3();
    for (auto bad : {
             "{1,2}",           // too few dimensions
             "{{{1}}}",         // too many dimensions
             "{{1,2},{3}}",     // not rectangular
             "{{1,2},{3,4}",    // unterminated
             "{{1,2},{3,4}}x",  // trailing garbage
             "{{1,2},3}",       // mixed
             "",
         })
        BOOST_TEST(field_parse(fv(bad), int4_array_oid, format_code::text, a));
    // Failed parses leave the output untouched
    check_2x3(a);
    BOOST_TEST_EQ(
        field_parse(field_view(), int4_array_oid, format_code::text, a),
        std::error_code(client_errc::unexpected_null)
    );
}

void test_parse_binary_2d()
{
    const auto data = bin({0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 23,  // header
                           0, 0, 0, 2, 0, 0, 0, 1,               // dim 0
                           0, 0, 0, 3, 0, 0, 0, 1,               // dim 1
                           0, 0, 0, 4, 0, 0, 0, 1, 0, 0, 0, 4,  0, 0, 0, 2, 0, 0, 0, 4, 0, 0, 0, 3,
                           0, 0, 0, 4, 0, 0, 0, 4, 0, 0, 0, 4,  0, 0, 0, 5, 0, 0, 0, 4, 0, 0, 0, 6});
    multi::array<std::int32_t, 2> a;
    BOOST_TEST_EQ(field_parse(fv(data), int4_array_oid, format_code::binary, a), std::error_code());
    check_2x3(a);
}

void test_parse_binary_empty_and_errors()
{
    multi::array<std::int32_t, 2> a = make_2x3();
    BOOST_TEST_EQ(
        field_parse(fv(bin({0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 23})), int4_array_oid, format_code::binary, a),
        std::error_code()
    );
    BOOST_TEST_EQ(a.num_elements(), 0);

    // 1-D data for a 2-D target
    BOOST_TEST(field_parse(
        fv(bin({0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 23, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 4, 0, 0, 0, 1})),
        int4_array_oid,
        format_code::binary,
        a
    ));
    // Huge dimensions
    BOOST_TEST(field_parse(
        fv(bin({0,    0,    0, 2, 0, 0, 0,    0,    0,    0,    0, 23, 0x7f, 0xff,
                0xff, 0xff, 0, 0, 0, 1, 0x7f, 0xff, 0xff, 0xff, 0, 0,  0,    1})),
        int4_array_oid,
        format_code::binary,
        a
    ));
    // Truncated
    BOOST_TEST_EQ(
        field_parse(
            fv(bin({0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 23, 0, 0, 0, 1})),
            int4_array_oid,
            format_code::binary,
            a
        ),
        std::error_code(client_errc::incomplete_message)
    );
}

void test_serialize_text()
{
    std::vector<unsigned char> buf;
    BOOST_TEST_EQ(field_serialize(make_2x3(), format_code::text, buf), std::error_code());
    BOOST_TEST_EQ(sv(buf), "{{1,2,3},{4,5,6}}");

    buf.clear();
    multi::array<std::optional<std::string>, 2> s({1, 2});
    s[0][0] = "a b";
    s[0][1] = std::nullopt;
    BOOST_TEST_EQ(field_serialize(s, format_code::text, buf), std::error_code());
    BOOST_TEST_EQ(sv(buf), R"({{"a b",NULL}})");

    buf.clear();
    multi::array<std::int32_t, 2> empty;
    BOOST_TEST_EQ(field_serialize(empty, format_code::text, buf), std::error_code());
    BOOST_TEST_EQ(sv(buf), "{}");
}

void test_serialize_binary()
{
    std::vector<unsigned char> buf;
    BOOST_TEST_EQ(field_serialize(make_2x3(), format_code::binary, buf), std::error_code());
    const auto expected = bin({0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 23,  //
                               0, 0, 0, 2, 0, 0, 0, 1,               //
                               0, 0, 0, 3, 0, 0, 0, 1,               //
                               0, 0, 0, 4, 0, 0, 0, 1, 0, 0, 0, 4,  0, 0, 0, 2, 0, 0, 0, 4, 0, 0, 0, 3,
                               0, 0, 0, 4, 0, 0, 0, 4, 0, 0, 0, 4,  0, 0, 0, 5, 0, 0, 0, 4, 0, 0, 0, 6});
    BOOST_TEST_EQ(sv(buf), expected);

    buf.clear();
    multi::array<std::int32_t, 3> empty;
    BOOST_TEST_EQ(field_serialize(empty, format_code::binary, buf), std::error_code());
    BOOST_TEST_EQ(sv(buf), bin({0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 23}));
}

void test_roundtrip()
{
    multi::array<std::optional<std::string>, 3> in({2, 2, 2});
    int n = 0;
    for (auto& v : in.elements())
    {
        ++n;
        if (n % 3 == 0)
            v = std::nullopt;
        else if (n % 3 == 1)
            v = "x,y " + std::to_string(n);
        else
            v = "";
    }

    for (auto code : {format_code::text, format_code::binary})
    {
        std::vector<unsigned char> buf;
        BOOST_TEST_EQ(field_serialize(in, code, buf), std::error_code());
        multi::array<std::optional<std::string>, 3> out;
        BOOST_TEST_EQ(
            field_parse(field_view(std::span<const unsigned char>(buf)), text_array_oid, code, out),
            std::error_code()
        );
        BOOST_TEST_EQ(out.num_elements(), 8);
        BOOST_TEST(in == out);
    }
}

}  // namespace

int main()
{
    test_is_compatible();
    test_parse_text_2d();
    test_parse_text_3d();
    test_parse_text_1d();
    test_parse_text_strings_and_nulls();
    test_parse_text_empty();
    test_parse_text_errors();
    test_parse_binary_2d();
    test_parse_binary_empty_and_errors();
    test_serialize_text();
    test_serialize_binary();
    test_roundtrip();

    return boost::report_errors();
}
