//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/core/lightweight_test.hpp>

#include <algorithm>
#include <span>
#include <utility>

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
    // Consuming does not reallocate
    const auto* ptr_to_5 = buff.committed_area().data() + 4u;
    BOOST_TEST_EQ(*ptr_to_5, 5u);
    buff.consume(3u);
    NATIVEPG_TEST_CONT_EQ(buff.committed_area(), std::span(data).subspan(3));
    BOOST_TEST_EQ(buff.prepared_area().size(), 4088u);
    BOOST_TEST_EQ(buff.committed_area().data() + 1u, ptr_to_5);
    BOOST_TEST_EQ(*ptr_to_5, 5u);
}

// Constructing a zero-sized buffer is not UB
void test_construct_zero_size()
{
    read_buffer buff{0u};
    BOOST_TEST_EQ(buff.committed_area().size(), 0u);
    BOOST_TEST_EQ(buff.prepared_area().size(), 1u);
}

// Constructing with size not a power of two rounds the size
void test_construct_rounds_size()
{
    // The requested size is rounded up to the next power of 2
    read_buffer buff{1000u};
    BOOST_TEST_EQ(buff.committed_area().size(), 0u);
    BOOST_TEST_EQ(buff.prepared_area().size(), 1024u);

    // An exact power of 2 is left untouched
    read_buffer exact{2048u};
    BOOST_TEST_EQ(exact.committed_area().size(), 0u);
    BOOST_TEST_EQ(exact.prepared_area().size(), 2048u);
}

// Moving, then move-assigning works OK
void test_move()
{
    constexpr unsigned char data[] = {10, 20, 30, 40};

    // A buffer with some committed data
    read_buffer buff{64u};
    copy_to(data, buff.prepared_area());
    buff.commit(4u);

    // Move-construct: the committed data and free space transfer to the new buffer
    read_buffer moved{std::move(buff)};
    NATIVEPG_TEST_CONT_EQ(moved.committed_area(), data);
    BOOST_TEST_EQ(moved.prepared_area().size(), 60u);

    // Move-assign into a different buffer: same transfer, old contents are released
    read_buffer assigned{8u};
    assigned = std::move(moved);
    NATIVEPG_TEST_CONT_EQ(assigned.committed_area(), data);
    BOOST_TEST_EQ(assigned.prepared_area().size(), 60u);
}

// Committing the entire prepared area works
void test_commit_entire_prepared_area()
{
    constexpr unsigned char data[] = {1, 2, 3, 4, 5, 6, 7, 8};

    // A buffer exactly the size of the data we'll write
    read_buffer buff{8u};
    copy_to(data, buff.prepared_area());

    // Commit all of it: everything moves to the committed area, no free space left
    buff.commit(8u);
    NATIVEPG_TEST_CONT_EQ(buff.committed_area(), data);
    BOOST_TEST_EQ(buff.prepared_area().size(), 0u);
}

// Committing size > prepared area commits the entire prepared area
void test_commit_over_prepared_area()
{
    constexpr unsigned char data[] = {1, 2, 3, 4, 5, 6, 7, 8};

    // Populate buffer
    read_buffer buff{8u};
    copy_to(data, buff.prepared_area());

    // Committing more than is available is clamped to the whole prepared area
    buff.commit(100u);
    NATIVEPG_TEST_CONT_EQ(buff.committed_area(), data);
    BOOST_TEST_EQ(buff.prepared_area().size(), 0u);
}

// Committing size == 0 is a no-op
void test_commit_zero()
{
    constexpr unsigned char data[] = {1, 2, 3, 4};

    // Setup
    read_buffer buff{16u};
    copy_to(data, buff.prepared_area());
    buff.commit(4u);

    // Committing 0 leaves both areas untouched
    buff.commit(0u);
    NATIVEPG_TEST_CONT_EQ(buff.committed_area(), data);
    BOOST_TEST_EQ(buff.prepared_area().size(), 12u);
}

// Two consecutive commits are OK
void test_two_commits()
{
    constexpr unsigned char data[] = {1, 2, 3, 4, 5, 6, 7, 8};

    read_buffer buff{16u};

    // First chunk
    copy_to(std::span(data).first(5), buff.prepared_area());
    buff.commit(5u);

    // Second chunk goes into the now-shrunk prepared area, right after the committed bytes
    copy_to(std::span(data).last(3), buff.prepared_area());
    buff.commit(3u);

    // Both chunks form a single contiguous committed area
    NATIVEPG_TEST_CONT_EQ(buff.committed_area(), data);
    BOOST_TEST_EQ(buff.prepared_area().size(), 8u);
}

// Consuming the entire committed area works
// Consuming size > committed area consumes the entire committed area
// Consuming size == 0 is a no-op
// Two consecutive consumes are OK
// Preparing < prepared size is a no-op
// Preparing size 0 is a no-op
// Preparing with a size > prepared size and no consumed area reallocates to the next power of 2
// Preparing with a size > prepared size and a committed area reallocates to the next power of 2 and copies
// the committed area Preparing with a size > prepared size and consumed area does not copy the consumed area.
//    The space taken by the consumed area counts as free space, and may condition the amount of allocated
//    space
// Preparing with a size > prepared size, consumed and committed area
// Preparing with a size > prepared size but < consumed area memmoves and doesn't reallocate
// Preparing with a size > prepared size but == consumed area memmoves and doesn't reallocate
// Two consecutive prepares are OK

}  // namespace

int main()
{
    test_usual_workflow();
    test_construct_zero_size();
    test_construct_rounds_size();
    test_move();
    test_commit_entire_prepared_area();
    test_commit_over_prepared_area();
    test_commit_zero();
    test_two_commits();

    return boost::report_errors();
}
