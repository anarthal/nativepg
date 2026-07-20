//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/core/lightweight_test.hpp>
#include <boost/json/parse.hpp>
#include <boost/json/serialize.hpp>
#include <boost/json/src.hpp>  // inline header-only implementation (single TU)
#include <boost/json/value.hpp>
#include <boost/system/error_code.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "nativepg/detail/field_traits.hpp"
#include "nativepg/protocol/describe.hpp"
#include "nativepg/types/json.hpp"
#include "test_utils/test_utils.hpp"

using namespace nativepg;

namespace {

// Builds a field_description with the given type OID and format code (the rest of the fields are
// irrelevant to type parsing)
protocol::field_description make_field_description(
    std::int32_t type_oid,
    protocol::format_code fmt_code = protocol::format_code::text
)
{
    return {
        .name = "field",
        .table_oid = 0,
        .column_attribute = 0,
        .type_oid = type_oid,
        .type_length = 0,
        .type_modifier = 0,
        .fmt_code = fmt_code,
    };
}

field_view make_field_view(const std::string_view& str)
{
    return field_view(
        std::span<const unsigned char>(reinterpret_cast<const unsigned char*>(str.data()), str.size())
    );
}

//
// types::parse_text_json / types::parse_binary_json
//
void test_parse_text_json_success()
{
    // Arrange
    boost::json::value out_val;
    const std::string str = boost::json::serialize(
        boost::json::parse(R"({"name":"John","age":30,"tags":["a","b"],"active":true,"score":null})")
    );

    // Act
    auto err = types::parse_json(str, out_val);

    // Assert
    NATIVEPG_TEST_EQ(err, boost::system::error_code{});
    NATIVEPG_TEST(out_val == boost::json::parse(str));
}

void test_parse_text_json_scalar_success()
{
    // Arrange: JSON allows any scalar as a top-level value (numbers, strings, bools, null).
    boost::json::value out_val;
    const std::string str = "42";

    // Act
    auto err = types::parse_json(str, out_val);

    // Assert
    NATIVEPG_TEST_EQ(err, boost::system::error_code{});
    NATIVEPG_TEST(out_val.is_number());
    NATIVEPG_TEST_EQ(out_val.to_number<int>(), 42);
}

// An empty (but non-NULL) field is treated as a no-op: parsing succeeds and `to` is left untouched.
// This keeps the function well-defined instead of failing/crashing on unexpected input, even though
// PostgreSQL never actually sends a zero-length json/jsonb value in practice.
void test_parse_text_json_empty_is_noop()
{
    // Arrange
    boost::json::value out_val("sentinel");
    // Note: bind the literal to a named variable first — make_field_view's field_view points into the
    // string's buffer, so the string must outlive it (a temporary bound directly to make_field_view's
    // by-value/by-const-ref parameter would be destroyed at the end of this statement, leaving fv
    // dangling for the rest of the function).
    const std::string str;

    // Act
    auto err = types::parse_json(str, out_val);

    // Assert
    NATIVEPG_TEST_EQ(err, boost::system::error_code{});
    NATIVEPG_TEST(out_val.is_string());
    NATIVEPG_TEST_EQ(out_val.as_string(), "sentinel");
}

void test_parse_text_json_malformed_error()
{
    // Arrange
    boost::json::value out_val;
    // Note: see test_parse_text_json_empty_is_noop for why str must be a named variable.
    const std::string str = "{not valid json";

    // Act
    auto err = types::parse_json(str, out_val);

    // Assert: Boost.JSON's parser error is surfaced verbatim (not translated to a nativepg error code).
    NATIVEPG_TEST(err.failed());
}

// Binary JSONB wire format: a single 0x01 version byte, followed by the JSON text (identical to the
// text-format wire representation).
void test_parse_binary_jsonb_success()
{
    // Arrange
    boost::json::value out_val;
    const std::string json_text = boost::json::serialize(boost::json::parse(R"({"a":1,"b":[true,false]})"));
    std::string wire;
    wire.push_back(static_cast<char>(1));
    wire += json_text;
    const auto fv = make_field_view(wire);

    // Act
    auto err = types::parse_binary_jsonb(fv, out_val);

    // Assert
    NATIVEPG_TEST_EQ(err, boost::system::error_code{});
    NATIVEPG_TEST(out_val == boost::json::parse(json_text));
}

void test_parse_binary_jsonb_null_error()
{
    // Arrange
    boost::json::value out_val;
    field_view fv;  // NULL

    // Act
    auto err = types::parse_binary_jsonb(fv, out_val);

    // Assert
    NATIVEPG_TEST_EQ(err, boost::system::error_code(client_errc::protocol_value_error));
}

void test_parse_binary_jsonb_bad_version_error()
{
    // Arrange
    boost::json::value out_val;
    const std::string wire = "\x02{}";
    const auto fv = make_field_view(wire);

    // Act
    auto err = types::parse_binary_jsonb(fv, out_val);

    // Assert
    NATIVEPG_TEST_EQ(err, boost::system::error_code(client_errc::protocol_value_error));
}

// A zero-length, non-NULL field (no version byte at all) must be rejected rather than reading past the
// end of the (possibly empty/null) underlying buffer.
void test_parse_binary_jsonb_too_short_error()
{
    // Arrange
    boost::json::value out_val;
    const std::vector<unsigned char> empty_data;
    field_view fv{std::span<const unsigned char>(empty_data)};

    // Act
    auto err = types::parse_binary_jsonb(fv, out_val);

    // Assert
    NATIVEPG_TEST_EQ(err, boost::system::error_code(client_errc::protocol_value_error));
}

// Version byte present, but no JSON payload after it: treated as the empty/no-op case, same as
// parse_text_jsonb with an empty string.
void test_parse_binary_jsonb_version_only_is_noop()
{
    // Arrange
    boost::json::value out_val("sentinel");
    const std::string wire(1, static_cast<char>(1));
    const auto fv = make_field_view(wire);

    // Act
    auto err = types::parse_binary_jsonb(fv, out_val);

    // Assert
    NATIVEPG_TEST_EQ(err, boost::system::error_code{});
    NATIVEPG_TEST(out_val.is_string());
    NATIVEPG_TEST_EQ(out_val.as_string(), "sentinel");
}

//
// detail::field_is_compatible / detail::field_parse (field_traits_json.hpp)
//
void test_field_is_compatible_json_success()
{
    NATIVEPG_TEST_EQ(
        detail::field_is_compatible<boost::json::value>::call(make_field_description(detail::json_oid)),
        boost::system::error_code{}
    );
}

void test_field_is_compatible_jsonb_success()
{
    NATIVEPG_TEST_EQ(
        detail::field_is_compatible<boost::json::value>::call(make_field_description(detail::jsonb_oid)),
        boost::system::error_code{}
    );
}

void test_field_is_compatible_incompatible_error()
{
    NATIVEPG_TEST_EQ(
        detail::field_is_compatible<boost::json::value>::call(make_field_description(23 /* int4 oid */)),
        boost::system::error_code(client_errc::incompatible_field_type)
    );
}

void test_field_parse_unexpected_null_error()
{
    // Arrange
    boost::json::value out_val;
    field_view fv;  // NULL
    const auto desc = make_field_description(detail::json_oid);

    // Act
    auto err = detail::field_parse<boost::json::value>::call(fv, desc, out_val);

    // Assert
    NATIVEPG_TEST_EQ(err, boost::system::error_code(client_errc::unexpected_null));
}

void test_field_parse_json_text_success()
{
    // Arrange
    boost::json::value out_val;
    const std::string str = R"({"k":"v"})";
    const auto fv = make_field_view(str);
    const auto desc = make_field_description(detail::json_oid, protocol::format_code::text);

    // Act
    auto err = detail::field_parse<boost::json::value>::call(fv, desc, out_val);

    // Assert
    NATIVEPG_TEST_EQ(err, boost::system::error_code{});
    NATIVEPG_TEST(out_val == boost::json::parse(str));
}

void test_field_parse_json_binary_success()
{
    // Arrange: json's "binary" wire format is the same as its text one (no version byte).
    boost::json::value out_val;
    const std::string str = R"([1,2,3])";
    const auto fv = make_field_view(str);
    const auto desc = make_field_description(detail::json_oid, protocol::format_code::binary);

    // Act
    auto err = detail::field_parse<boost::json::value>::call(fv, desc, out_val);

    // Assert
    NATIVEPG_TEST_EQ(err, boost::system::error_code{});
    NATIVEPG_TEST(out_val == boost::json::parse(str));
}

void test_field_parse_jsonb_text_success()
{
    // Arrange
    boost::json::value out_val;
    const std::string str = R"({"k":"v"})";
    const auto fv = make_field_view(str);
    const auto desc = make_field_description(detail::jsonb_oid, protocol::format_code::text);

    // Act
    auto err = detail::field_parse<boost::json::value>::call(fv, desc, out_val);

    // Assert
    NATIVEPG_TEST_EQ(err, boost::system::error_code{});
    NATIVEPG_TEST(out_val == boost::json::parse(str));
}

void test_field_parse_jsonb_binary_success()
{
    // Arrange: jsonb's binary wire format has a leading 0x01 version byte.
    boost::json::value out_val;
    const std::string json_text = R"({"k":"v"})";
    std::string wire;
    wire.push_back(static_cast<char>(1));
    wire += json_text;
    const auto fv = make_field_view(wire);
    const auto desc = make_field_description(detail::jsonb_oid, protocol::format_code::binary);

    // Act
    auto err = detail::field_parse<boost::json::value>::call(fv, desc, out_val);

    // Assert
    NATIVEPG_TEST_EQ(err, boost::system::error_code{});
    NATIVEPG_TEST(out_val == boost::json::parse(json_text));
}

}  // namespace

int main()
{
    test_parse_text_json_success();
    test_parse_text_json_scalar_success();
    test_parse_text_json_empty_is_noop();
    test_parse_text_json_malformed_error();

    test_parse_binary_jsonb_success();
    test_parse_binary_jsonb_null_error();
    test_parse_binary_jsonb_bad_version_error();
    test_parse_binary_jsonb_too_short_error();
    test_parse_binary_jsonb_version_only_is_noop();

    test_field_is_compatible_json_success();
    test_field_is_compatible_jsonb_success();
    test_field_is_compatible_incompatible_error();
    test_field_parse_unexpected_null_error();
    test_field_parse_json_text_success();
    test_field_parse_json_binary_success();
    test_field_parse_jsonb_text_success();
    test_field_parse_jsonb_binary_success();

    return boost::report_errors();
}
