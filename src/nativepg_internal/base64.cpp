//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/assert.hpp>
#include <boost/core/span.hpp>
#include <boost/system/detail/error_code.hpp>

#include <cstddef>
#include <string_view>

#include "base64.hpp"
#include "nativepg/client_errc.hpp"

// This code has been copied and adapted from Boost.Beast implementation, since the
// interface is not public.

using namespace nativepg;

namespace {

// Number of bytes of expected padding for a given input data length
std::size_t get_padding(std::size_t data_length)
{
    switch (data_length % 3)
    {
    case 1: return 2;
    case 2: return 1;
    default: return 0;
    }
}

// Returns the number of bytes needed to encode the given input
std::size_t encoded_size(boost::span<const unsigned char> input) { return 4 * ((input.size() + 2) / 3); }

// Gets the number of padding characters at the end of input.
// input should be at least 4 characters long
std::size_t get_padding(std::string_view input)
{
    BOOST_ASSERT(input.size() >= 4u);
    return (input.back() == '=') + (input[input.size() - 2u] == '=');
}

// Returns the number of bytes that the decoded form of the given input would have.
// Input size should be divisible by 4
std::size_t decoded_size(std::string_view input)
{
    BOOST_ASSERT(input.size() % 4 == 0u);
    return input.empty() ? 0u : input.size() * 3 / 4 - get_padding(input);
}

// Forward table
constexpr char alphabet[] = {
    "ABCDEFGHIJKLMNOP"
    "QRSTUVWXYZabcdef"
    "ghijklmnopqrstuv"
    "wxyz0123456789+/"
};

// Inverse table
constexpr signed char inverse_tab[] = {
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  //   0-15
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  //  16-31
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 62, -1, -1, -1, 63,  //  32-47
    52, 53, 54, 55, 56, 57, 58, 59, 60, 61, -1, -1, -1, -1, -1, -1,  //  48-63
    -1, 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14,  //  64-79
    15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1, -1,  //  80-95
    -1, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,  //  96-111
    41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, -1, -1, -1, -1, -1,  // 112-127
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  // 128-143
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  // 144-159
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  // 160-175
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  // 176-191
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  // 192-207
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  // 208-223
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  // 224-239
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1   // 240-255
};

// Encodes src and stores it in dest. dest must point to encoded_size(src) bytes.
// Returns the number of written characters
void encode(boost::span<const unsigned char> src, char* dest)
{
    char* out = dest;
    const unsigned char* in = src.data();
    std::size_t len = src.size();
    const char* tab = alphabet;

    for (auto n = len / 3; n--;)
    {
        *out++ = tab[(in[0] & 0xfc) >> 2];
        *out++ = tab[((in[0] & 0x03) << 4) + ((in[1] & 0xf0) >> 4)];
        *out++ = tab[((in[2] & 0xc0) >> 6) + ((in[1] & 0x0f) << 2)];
        *out++ = tab[in[2] & 0x3f];
        in += 3;
    }

    switch (len % 3)
    {
    case 2:
        *out++ = tab[(in[0] & 0xfc) >> 2];
        *out++ = tab[((in[0] & 0x03) << 4) + ((in[1] & 0xf0) >> 4)];
        *out++ = tab[(in[1] & 0x0f) << 2];
        *out++ = '=';
        break;

    case 1:
        *out++ = tab[(in[0] & 0xfc) >> 2];
        *out++ = tab[((in[0] & 0x03) << 4)];
        *out++ = '=';
        *out++ = '=';
        break;

    case 0: break;
    }
}

// Decodes src and stores it in dest. dest must point to decoded_size(from) bytes.
// Retuns true on success.
static bool decode(std::string_view from, unsigned char* dest)
{
    // Base64 strings must have a size multiple of 4
    BOOST_ASSERT(from.size() % 4u == 0u);

    // If the input is empty, there is nothing to do
    if (from.empty())
        return true;

    // Decode everything except for the last four characters
    const char* p_last = from.data() + from.size() - 4u;
    for (const char* p = from.data(); p < p_last; p += 4u, dest += 3u)
    {
        // Look up in the reverse table
        const auto v0 = inverse_tab[static_cast<std::size_t>(*p)];
        const auto v1 = inverse_tab[static_cast<std::size_t>(*(p + 1))];
        const auto v2 = inverse_tab[static_cast<std::size_t>(*(p + 2))];
        const auto v3 = inverse_tab[static_cast<std::size_t>(*(p + 3))];

        // Check that the characters were valid
        if (v0 == -1 || v1 == -1 || v2 == -1 || v3 == -1)
            return false;

        // Write to the output
        dest[0] = (v0 << 2) + ((v1 & 0x30) >> 4);
        dest[1] = ((v1 & 0xf) << 4) + ((v2 & 0x3c) >> 2);
        dest[2] = ((v2 & 0x3) << 6) + v3;
    }

    // Decode the last four characters
    std::size_t padding = get_padding(from);
    const auto v0 = inverse_tab[static_cast<std::size_t>(*p_last)];
    const auto v1 = inverse_tab[static_cast<std::size_t>(*(p_last + 1))];
    const auto v2 = padding == 2u ? 0u : inverse_tab[static_cast<std::size_t>(*(p_last + 2))];
    const auto v3 = padding > 0u ? 0u : inverse_tab[static_cast<std::size_t>(*(p_last + 3))];

    // Check that the characters were valid
    if (v0 == -1 || v1 == -1 || v2 == -1 || v3 == -1)
        return false;

    // Write to the output
    dest[0] = (v0 << 2) + ((v1 & 0x30) >> 4);
    if (padding < 2u)
        dest[1] = ((v1 & 0xf) << 4) + ((v2 & 0x3c) >> 2);
    if (padding == 0u)
        dest[2] = ((v2 & 0x3) << 6) + v3;

    // Done
    return true;
}

}  // namespace

void nativepg::protocol::detail::base64_encode(
    boost::span<const unsigned char> input,
    std::vector<unsigned char>& to
)
{
    // Reserve size
    std::size_t size_before = to.size();
    to.resize(to.size() + encoded_size(input));

    // Actually encode
    encode(input, reinterpret_cast<char*>(to.data() + size_before));
}

boost::system::error_code nativepg::protocol::detail::base64_decode(
    boost::span<const unsigned char> input,
    std::vector<unsigned char>& output
)
{
    // Convert the input
    std::string_view input_str(reinterpret_cast<const char*>(input.data()), input.size());

    // Check that the size is valid
    if (input_str.size() % 4u != 0u)
        return client_errc::invalid_base64;

    // Reserve the size
    std::size_t size_before = output.size();
    output.resize(output.size() + decoded_size(input_str));

    // Actually decode
    bool ok = decode(input_str, output.data() + size_before);

    // Convert to error code
    return ok ? boost::system::error_code() : client_errc::invalid_base64;
}