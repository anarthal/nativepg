//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/core/lightweight_test.hpp>
#include <boost/system/error_code.hpp>
#include <boost/multiprecision/cpp_dec_float.hpp>

#include <string>
#include <sstream>
#include <iomanip>
#include <limits>
#include <vector>
#include <span>

#include "nativepg/detail/field_traits.hpp"
#include "nativepg/protocol/describe.hpp"

#include "nativepg/types/base.hpp"

using namespace nativepg;

namespace {

// BOOL
void test__parse_text_bool__t_success()
{
    // Arrange
    bool b = false;
    std::string str = "t";
    boost::span<const unsigned char> data(reinterpret_cast<const unsigned char*>(str.data()), str.size());

    // Act
    auto err = types::parse_text_bool(data, b);

    // Assert
    BOOST_TEST_EQ(err, boost::system::errc::success);
    BOOST_TEST_EQ(b, true);
}

void test__parse_text_bool__true_success()
{
    // Arrange
    bool b = false;
    std::string str = "true";
    boost::span<const unsigned char> data(reinterpret_cast<const unsigned char*>(str.data()), str.size());

    // Act
    auto err = types::parse_text_bool(data, b);

    // Assert
    BOOST_TEST_EQ(err, boost::system::errc::success);
    BOOST_TEST_EQ(b, true);
}

void test__parse_binary_bool__t_success()
{
    // Arrange
    bool b = false;
    static constexpr unsigned char pg_bool[] = {
        0x1
    };
    boost::span<const unsigned char> data(pg_bool);

    // Act
    auto err = types::parse_binary_bool(data, b);

    // Assert
    BOOST_TEST_EQ(err, boost::system::errc::success);
    BOOST_TEST_EQ(b, true);
}

void test__parse_text_bool__f_success()
{
    // Arrange
    bool b = true;
    std::string str = "f";
    boost::span<const unsigned char> data(reinterpret_cast<const unsigned char*>(str.data()), str.size());

    // Act
    auto err = types::parse_text_bool(data, b);

    // Assert
    BOOST_TEST_EQ(err, boost::system::errc::success);
    BOOST_TEST_EQ(b, false);
}

void test__parse_text_bool__false_success()
{
    // Arrange
    bool b = true;
    std::string str = "false";
    boost::span<const unsigned char> data(reinterpret_cast<const unsigned char*>(str.data()), str.size());

    // Act
    auto err = types::parse_text_bool(data, b);

    // Assert
    BOOST_TEST_EQ(err, boost::system::errc::success);
    BOOST_TEST_EQ(b, false);
}

void test__parse_binary_bool__f_success()
{
    // Arrange
    bool b = true;
    static constexpr unsigned char pg_bool[] = {
        0x0
    };
    boost::span<const unsigned char> data(pg_bool);

    // Act
    auto err = types::parse_binary_bool(data, b);

    // Assert
    BOOST_TEST_EQ(err, boost::system::errc::success);
    BOOST_TEST_EQ(b, false);
}


// BYTEA
void test__parse_text_bytea__success()
{
    // Arrange
    std::vector<std::byte> ba;
    std::string str = "\\x21061977";
    boost::span<const unsigned char> data(reinterpret_cast<const unsigned char*>(str.data()), str.size());

    // Act
    auto err = types::parse_text_bytea(data, ba);

    // Assert
    BOOST_TEST_EQ(err, boost::system::errc::success);
    BOOST_TEST_EQ(ba.size(), 4);
    std::stringstream ss;
    ss << ba;
    BOOST_TEST_EQ(ss.str(), "0x21061977");
}

void test_parse_binary_bytea_success()
{
    // Arrange
    std::vector<std::byte> ba;
    static constexpr unsigned char pg_bytea[] = {
        0x21, 0x06, 0x19, 0x77
    };
    boost::span<const unsigned char> data(pg_bytea);

    // Act
    auto err = types::parse_binary_bytea(data, ba);

    // Assert
    BOOST_TEST_EQ(err, boost::system::errc::success);
    BOOST_TEST_EQ(ba.size(), 4);
    std::stringstream ss;
    ss << ba;
    BOOST_TEST_EQ(ss.str(), "0x21061977");
}

// INT
template <typename T, T in_val>
void test__parse_text_int__success()
{
    // Arrange
    T out_val;
    std::string str = std::to_string(in_val);
    boost::span<const unsigned char> data(reinterpret_cast<const unsigned char*>(str.data()), str.size());

    // Act
    auto err = types::parse_text_int<T>(data, out_val);

    // Assert
    BOOST_TEST_EQ(err, boost::system::errc::success);
    BOOST_TEST_EQ(out_val, in_val);
}

template <typename T, T in_val>
void test__parse_binary_int__success()
{
    // Arrange
    T out_val;
    std::vector<uint8_t> buffer(sizeof(T));
    boost::endian::endian_store<T, sizeof(T), boost::endian::order::big>(buffer.data(), in_val);

    // Act
    auto err = types::parse_binary_int<T>(buffer, out_val);

    // Assert
    BOOST_TEST_EQ(err, boost::system::errc::success);
    BOOST_TEST_EQ(out_val, in_val);
}


// FLOAT
template <typename T, T in_val>
void test__parse_text_float__success()
{
    // Arrange
    T out_val;
    std::stringstream ss;
    ss << std::setprecision(std::numeric_limits<T>::max_digits10) << in_val;
    std::string str = ss.str();
    boost::span<const unsigned char> data(reinterpret_cast<const unsigned char*>(str.data()), str.size());

    // Act
    auto err = types::parse_text_float<T>(data, out_val);

    // Assert
    BOOST_TEST_EQ(err, boost::system::errc::success);
    BOOST_TEST_EQ(out_val, in_val);
}

template <typename T, T in_val>
void test__parse_binary_float__success()
{
    // Arrange
    T out_val;
    std::vector<uint8_t> buffer(sizeof(T));
    boost::endian::endian_store<T, sizeof(T), boost::endian::order::big>(buffer.data(), in_val);

    // Act
    auto err = types::parse_binary_float<T>(buffer, out_val);

    // Assert
    BOOST_TEST_EQ(err, boost::system::errc::success);
    BOOST_TEST_EQ(out_val, in_val);
}

template <typename T = std::string>
void test_parse_text_text_success(const T& in_val)
{
    // Arrange
    T out_val;
    const std::string str = in_val;
    boost::span<const unsigned char> data(reinterpret_cast<const unsigned char*>(str.data()), str.size());

    // Act
    auto err = types::parse_text_text<T>(data, out_val);

    // Assert
    BOOST_TEST_EQ(err, boost::system::errc::success);
    BOOST_TEST_EQ(out_val, in_val);
}

template <typename T = std::string>
void test__parse_binary_text__success(const T& in_val)
{
    // Arrange
    T out_val;
    const std::string_view str = in_val;
    boost::span<const unsigned char> data(reinterpret_cast<const unsigned char*>(str.data()), str.size());

    // Act
    auto err = types::parse_binary_text<T>(data, out_val);

    // Assert
    BOOST_TEST_EQ(err, boost::system::errc::success);
    BOOST_TEST_EQ(out_val, in_val);

    if constexpr (std::is_same_v<T, std::string_view>)
        BOOST_TEST_EQ((const void*)out_val.data(), (const void*)data.data());  // zero-copy: aliases input
    else
        BOOST_TEST_NE((const void*)out_val.data(), (const void*)data.data());  // std::string: owns a copy
}

}  // namespace

int main()
{
    // BOOL
    test__parse_text_bool__t_success();
    test__parse_text_bool__true_success();
    test__parse_binary_bool__t_success();
    test__parse_text_bool__f_success();
    test__parse_text_bool__false_success();
    test__parse_binary_bool__f_success();

    // BYTEA
    test__parse_text_bytea__success();
    test_parse_binary_bytea_success();

    // INT2
    test__parse_text_int__success<std::int16_t, std::numeric_limits<std::int16_t>::min()>();
    test__parse_text_int__success<std::int16_t, std::numeric_limits<std::int16_t>::max()>();
    test__parse_binary_int__success<std::int16_t, std::numeric_limits<std::int16_t>::min()>();
    test__parse_binary_int__success<std::int16_t, std::numeric_limits<std::int16_t>::max()>();
    // INT4
    test__parse_text_int__success<std::int32_t, std::numeric_limits<std::int32_t>::min()>();
    test__parse_text_int__success<std::int32_t, std::numeric_limits<std::int32_t>::max()>();
    test__parse_binary_int__success<std::int32_t, std::numeric_limits<std::int32_t>::min()>();
    test__parse_binary_int__success<std::int32_t, std::numeric_limits<std::int32_t>::max()>();
    // INT8
    test__parse_text_int__success<std::int64_t, std::numeric_limits<std::int64_t>::min()>();
    test__parse_text_int__success<std::int64_t, std::numeric_limits<std::int64_t>::max()>();
    test__parse_binary_int__success<std::int64_t, std::numeric_limits<std::int64_t>::min()>();
    test__parse_binary_int__success<std::int64_t, std::numeric_limits<std::int64_t>::max()>();

    // FLOAT4
    test__parse_text_float__success<float, std::numeric_limits<float>::min()>();
    test__parse_text_float__success<float, std::numeric_limits<float>::max()>();
    test__parse_binary_float__success<float, std::numeric_limits<float>::min()>();
    test__parse_binary_float__success<float, std::numeric_limits<float>::max()>();
    // FLOAT8
    test__parse_text_float__success<double, std::numeric_limits<double>::min()>();
    test__parse_text_float__success<double, std::numeric_limits<double>::max()>();
    test__parse_binary_float__success<double, std::numeric_limits<double>::min()>();
    test__parse_binary_float__success<double, std::numeric_limits<double>::max()>();

    // TEXT / VARCHAR
    test_parse_text_text_success<std::string>("The quick brown fox jumps over the lazy dog!");
    test__parse_binary_text__success<std::string>("The quick brown fox jumps over the lazy dog!");

    return boost::report_errors();
}
