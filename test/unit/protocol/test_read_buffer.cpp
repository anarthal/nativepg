//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/core/lightweight_test.hpp>

#include <algorithm>
#include <cstddef>
#include <limits>
#include <span>

#include "nativepg/protocol/detail/read_buffer.hpp"
#include "test_utils.hpp"

using namespace nativepg;
using protocol::detail::read_buffer;

namespace {

void copy_to(std::span<const unsigned char> data, std::span<unsigned char> to)
{
    BOOST_ASSERT(data.size() <= to.size());
    std::ranges::copy(data, to.data());
}

void test_usual_workflow()
{
    // Constructor: creates a buffer with free space
    read_buffer buff{4096u};
    BOOST_TEST_EQ(buff.committed_area().size(), 0u);
    BOOST_TEST_EQ(buff.prepared_area().size(), 4096u);

    // Prepare the buffer to fit a small message: nothing happens
    buff.prepare(1000u);
    BOOST_TEST_EQ(buff.committed_area().size(), 0u);
    BOOST_TEST_EQ(buff.prepared_area().size(), 4096u);

    // Read some data into the buffer and commit => bytes go into the committed area
    constexpr unsigned char data[] = {1, 2, 3, 4, 5, 6, 7, 8};
    copy_to(data, buff.prepared_area());
    buff.commit(8u);
    NATIVEPG_TEST_CONT_EQ(buff.committed_area(), data);
    BOOST_TEST_EQ(buff.prepared_area().size(), 4088u);

    // Consuming part of the bytes moves them to the consumed area
    buff.consume(3u);
    NATIVEPG_TEST_CONT_EQ(buff.committed_area(), std::span(data).subspan(3));
    BOOST_TEST_EQ(buff.prepared_area().size(), 4088u);
}

}  // namespace

int main()
{
    test_usual_workflow();

    return boost::report_errors();
}
