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

template <class T = std::vector<std::byte>>
static inline std::basic_ostream<T>& operator<<(std::basic_ostream<T>& os, const std::vector<std::byte>& bytes)
{
    if (bytes.empty() || (bytes.size() == 1 && bytes[0] == std::byte{0x00}))
    {
        os << "0x00";
    }
    else
    {
        os << "0x";
        for (auto byte : bytes)
            os << std::format("{:02x}", static_cast<int>(byte));
    }

    return os;
}

// BOOL
void test_parse_text_bool_t_success()
{
    // Arrange
    bool b = false;
    std::string str = "t";
    boost::span<const unsigned char> data(reinterpret_cast<const unsigned char*>(str.data()), str.size());
    field_view fv{data};

    // Act
    auto err = types::parse_text_bool(fv, b);

    // Assert
    BOOST_TEST_EQ(err, boost::system::errc::success);
    BOOST_TEST_EQ(b, true);
}

void test_parse_binary_bool_t_success()
{
    // Arrange
    bool b = false;
    static constexpr unsigned char pg_bool[] = {
        0x1
    };
    boost::span<const unsigned char> data(pg_bool);
    field_view fv{data};

    // Act
    auto err = types::parse_binary_bool(fv, b);

    // Assert
    BOOST_TEST_EQ(err, boost::system::errc::success);
    BOOST_TEST_EQ(b, true);
}

void test_parse_text_bool_f_success()
{
    // Arrange
    bool b = true;
    std::string str = "f";
    boost::span<const unsigned char> data(reinterpret_cast<const unsigned char*>(str.data()), str.size());
    field_view fv{data};

    // Act
    auto err = types::parse_text_bool(fv, b);

    // Assert
    BOOST_TEST_EQ(err, boost::system::errc::success);
    BOOST_TEST_EQ(b, false);
}

void test_parse_binary_bool_f_success()
{
    // Arrange
    bool b = true;
    static constexpr unsigned char pg_bool[] = {
        0x0
    };
    boost::span<const unsigned char> data(pg_bool);
    field_view fv{data};

    // Act
    auto err = types::parse_binary_bool(fv, b);

    // Assert
    BOOST_TEST_EQ(err, boost::system::errc::success);
    BOOST_TEST_EQ(b, false);
}


// BYTEA
void test_parse_text_bytea_success()
{
    // Arrange
    std::vector<std::byte> ba;
    std::string str = "\\x21061977";
    boost::span<const unsigned char> data(reinterpret_cast<const unsigned char*>(str.data()), str.size());
    field_view fv{data};

    // Act
    auto err = types::parse_text_bytea(fv, ba);

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
    field_view fv{data};

    // Act
    auto err = types::parse_binary_bytea(fv, ba);

    // Assert
    BOOST_TEST_EQ(err, boost::system::errc::success);
    BOOST_TEST_EQ(ba.size(), 4);
    std::stringstream ss;
    ss << ba;
    BOOST_TEST_EQ(ss.str(), "0x21061977");
}

// INT
template <typename T, T in_val>
void test_parse_text_int_success()
{
    // Arrange
    T out_val;
    std::string str = std::to_string(in_val);
    boost::span<const unsigned char> data(reinterpret_cast<const unsigned char*>(str.data()), str.size());
    field_view fv{data};

    // Act
    auto err = types::parse_text_int<T>(fv, out_val);

    // Assert
    BOOST_TEST_EQ(err, boost::system::errc::success);
    BOOST_TEST_EQ(out_val, in_val);
}

template <typename T, T in_val>
void test_parse_binary_int_success()
{
    // Arrange
    T out_val;
    std::vector<uint8_t> buffer(sizeof(T));
    boost::endian::endian_store<T, sizeof(T), boost::endian::order::big>(buffer.data(), in_val);
    field_view fv{buffer};

    // Act
    auto err = types::parse_binary_int<T>(fv, out_val);

    // Assert
    BOOST_TEST_EQ(err, boost::system::errc::success);
    BOOST_TEST_EQ(out_val, in_val);
}


// FLOAT
template <typename T, T in_val>
void test_parse_text_float_success()
{
    // Arrange
    T out_val;
    std::stringstream ss;
    ss << std::setprecision(std::numeric_limits<T>::max_digits10) << in_val;
    std::string str = ss.str();
    boost::span<const unsigned char> data(reinterpret_cast<const unsigned char*>(str.data()), str.size());
    field_view fv{data};

    // Act
    auto err = types::parse_text_float<T>(fv, out_val);

    // Assert
    BOOST_TEST_EQ(err, boost::system::errc::success);
    BOOST_TEST_EQ(out_val, in_val);
}

template <typename T, T in_val>
void test_parse_binary_float_success()
{
    // Arrange
    T out_val;
    std::vector<uint8_t> buffer(sizeof(T));
    boost::endian::endian_store<T, sizeof(T), boost::endian::order::big>(buffer.data(), in_val);
    field_view fv{buffer};

    // Act
    auto err = types::parse_binary_float<T>(fv, out_val);

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
    field_view fv{data};

    // Act
    auto err = types::parse_text_text<T>(fv, out_val);

    // Assert
    BOOST_TEST_EQ(err, boost::system::errc::success);
    BOOST_TEST_EQ(out_val, in_val);
}

template <typename T = std::string>
void test_parse_binary_text_success(const T& in_val)
{
    // Arrange
    T out_val;
    const std::string_view str = in_val;
    boost::span<const unsigned char> data(reinterpret_cast<const unsigned char*>(str.data()), str.size());
    field_view fv{data};

    // Act
    auto err = types::parse_binary_text<T>(fv, out_val);

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
    test_parse_text_bool_t_success();
    test_parse_binary_bool_t_success();
    test_parse_text_bool_f_success();
    test_parse_binary_bool_f_success();

    // BYTEA
    test_parse_text_bytea_success();
    test_parse_binary_bytea_success();

    // INT2
    test_parse_text_int_success<std::int16_t, std::numeric_limits<std::int16_t>::min()>();
    test_parse_text_int_success<std::int16_t, std::numeric_limits<std::int16_t>::max()>();
    test_parse_binary_int_success<std::int16_t, std::numeric_limits<std::int16_t>::min()>();
    test_parse_binary_int_success<std::int16_t, std::numeric_limits<std::int16_t>::max()>();
    // INT4
    test_parse_text_int_success<std::int32_t, std::numeric_limits<std::int32_t>::min()>();
    test_parse_text_int_success<std::int32_t, std::numeric_limits<std::int32_t>::max()>();
    test_parse_binary_int_success<std::int32_t, std::numeric_limits<std::int32_t>::min()>();
    test_parse_binary_int_success<std::int32_t, std::numeric_limits<std::int32_t>::max()>();
    // INT8
    test_parse_text_int_success<std::int64_t, std::numeric_limits<std::int64_t>::min()>();
    test_parse_text_int_success<std::int64_t, std::numeric_limits<std::int64_t>::max()>();
    test_parse_binary_int_success<std::int64_t, std::numeric_limits<std::int64_t>::min()>();
    test_parse_binary_int_success<std::int64_t, std::numeric_limits<std::int64_t>::max()>();

    // FLOAT4
    test_parse_text_float_success<float, std::numeric_limits<float>::min()>();
    test_parse_text_float_success<float, std::numeric_limits<float>::max()>();
    test_parse_binary_float_success<float, std::numeric_limits<float>::min()>();
    test_parse_binary_float_success<float, std::numeric_limits<float>::max()>();
    // FLOAT8
    test_parse_text_float_success<double, std::numeric_limits<double>::min()>();
    test_parse_text_float_success<double, std::numeric_limits<double>::max()>();
    test_parse_binary_float_success<double, std::numeric_limits<double>::min()>();
    test_parse_binary_float_success<double, std::numeric_limits<double>::max()>();

    // TEXT / VARCHAR
    test_parse_text_text_success<std::string>("The quick brown fox jumps over the lazy dog!");
    test_parse_binary_text_success<std::string>("The quick brown fox jumps over the lazy dog!");

    return boost::report_errors();
}
