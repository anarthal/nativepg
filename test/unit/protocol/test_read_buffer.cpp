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
#include "test_utils/test_utils.hpp"

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
void test_consume_entire_committed_area()
{
    constexpr unsigned char data[] = {1, 2, 3, 4, 5, 6, 7, 8};

    // Setup
    read_buffer buff{32u};
    copy_to(data, buff.prepared_area());
    buff.commit(8u);

    // Consume everything: the committed area empties. The prepared area is unchanged,
    // since consumed bytes are not returned as free space.
    buff.consume(8u);
    BOOST_TEST_EQ(buff.committed_area().size(), 0u);
    BOOST_TEST_EQ(buff.prepared_area().size(), 24u);
}

// Consuming size > committed area consumes the entire committed area
void test_consume_over_committed_area()
{
    constexpr unsigned char data[] = {1, 2, 3, 4, 5, 6, 7, 8};

    read_buffer buff{32u};
    copy_to(data, buff.prepared_area());
    buff.commit(8u);

    // Consuming more than is committed is clamped to the whole committed area
    buff.consume(100u);
    BOOST_TEST_EQ(buff.committed_area().size(), 0u);
    BOOST_TEST_EQ(buff.prepared_area().size(), 24u);
}

// Consuming size == 0 is a no-op
void test_consume_zero()
{
    constexpr unsigned char data[] = {1, 2, 3, 4};

    read_buffer buff{16u};
    copy_to(data, buff.prepared_area());
    buff.commit(4u);

    // Consuming 0 leaves both areas untouched
    buff.consume(0u);
    NATIVEPG_TEST_CONT_EQ(buff.committed_area(), data);
    BOOST_TEST_EQ(buff.prepared_area().size(), 12u);
}

// Two consecutive consumes are OK
void test_two_consumes()
{
    constexpr unsigned char data[] = {1, 2, 3, 4, 5, 6, 7, 8};

    read_buffer buff{16u};
    copy_to(data, buff.prepared_area());
    buff.commit(8u);

    // First consume: the committed area shrinks from the front
    buff.consume(5u);
    NATIVEPG_TEST_CONT_EQ(buff.committed_area(), std::span(data).last(3));
    BOOST_TEST_EQ(buff.prepared_area().size(), 8u);

    // Second consume: the remaining committed bytes are consumed too
    buff.consume(3u);
    BOOST_TEST_EQ(buff.committed_area().size(), 0u);
    BOOST_TEST_EQ(buff.prepared_area().size(), 8u);
}

// Preparing < prepared size is a no-op
void test_prepare_below_prepared_size()
{
    constexpr unsigned char data[] = {1, 2, 3, 4};

    read_buffer buff{16u};
    copy_to(data, buff.prepared_area());
    buff.commit(4u);

    // Capture the current location to detect (the absence of) reallocation
    const auto* data_ptr = buff.committed_area().data();

    // Requesting less than the available free space changes nothing
    buff.prepare(8u);
    NATIVEPG_TEST_CONT_EQ(buff.committed_area(), data);
    BOOST_TEST_EQ(buff.prepared_area().size(), 12u);
    BOOST_TEST_EQ(buff.committed_area().data(), data_ptr);
}

// Preparing size 0 is a no-op
void test_prepare_zero()
{
    constexpr unsigned char data[] = {1, 2, 3, 4};

    read_buffer buff{16u};
    copy_to(data, buff.prepared_area());
    buff.commit(4u);

    // Capture the current location to detect (the absence of) reallocation
    const auto* data_ptr = buff.committed_area().data();

    // Requesting 0 free bytes changes nothing
    buff.prepare(0u);
    NATIVEPG_TEST_CONT_EQ(buff.committed_area(), data);
    BOOST_TEST_EQ(buff.prepared_area().size(), 12u);
    BOOST_TEST_EQ(buff.committed_area().data(), data_ptr);
}

// Preparing with a size > prepared size reallocates to the next power of 2 (no committed, no consumed areas)
void test_prepare_reallocates_empty()
{
    read_buffer buff{16u};

    // Requesting more than the current free space reallocates, rounding up to the next power of 2
    buff.prepare(20u);
    BOOST_TEST_EQ(buff.committed_area().size(), 0u);
    BOOST_TEST_EQ(buff.prepared_area().size(), 32u);
}

// Preparing with a size > prepared size area reallocates.
// The committed area is copied.
void test_prepare_reallocates_with_committed()
{
    constexpr unsigned char data[] = {1, 2, 3, 4, 5, 6, 7, 8};

    // Fill the buffer completely so there's no free space left
    read_buffer buff{8u};
    copy_to(data, buff.prepared_area());
    buff.commit(8u);

    // Requesting more space reallocates and copies the committed bytes
    buff.prepare(10u);
    NATIVEPG_TEST_CONT_EQ(buff.committed_area(), data);
    BOOST_TEST_EQ(buff.prepared_area().size(), 24u);
}

// Preparing with a size > prepared size reallocates. The consumed area is not copied.
void test_prepare_reallocates_drops_consumed()
{
    constexpr unsigned char data[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};

    // Fill a 16-byte buffer, then consume the first half: 8 consumed, 8 committed, 0 free
    read_buffer buff{16u};
    copy_to(data, buff.prepared_area());
    buff.commit(16u);
    buff.consume(8u);

    // Reallocate. The new size only accounts for the committed area + required:
    //   next_power_of_2(8 + 24) == 32, so the prepared area is 32 - 8 == 24.
    // If the 8 consumed bytes were (wrongly) copied and counted, the size would be
    //   next_power_of_2(8 + 8 + 24) == 64, giving a 56-byte prepared area instead.
    buff.prepare(24u);
    NATIVEPG_TEST_CONT_EQ(buff.committed_area(), std::span(data).last(8));
    BOOST_TEST_EQ(buff.prepared_area().size(), 24u);
}

// Preparing with a size > prepared size, consumed and committed area
void test_prepare_reallocates_consumed_and_committed()
{
    constexpr unsigned char data[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};

    // Set up all three areas: 4 consumed, 8 committed, 4 free
    read_buffer buff{16u};
    copy_to(data, buff.prepared_area());
    buff.commit(12u);
    buff.consume(4u);

    // Reallocate (4 consumed + 4 free = 8 < 24 requested) => reallocates to 32
    buff.prepare(24u);
    NATIVEPG_TEST_CONT_EQ(buff.committed_area(), std::span(data).last(8));
    BOOST_TEST_EQ(buff.prepared_area().size(), 24u);
}

// Preparing with a size > prepared size but < consumed area memmoves and doesn't reallocate
void test_prepare_memmoves()
{
    constexpr unsigned char data[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14};

    // Set up: 8 consumed, 6 committed, 2 free
    read_buffer buff{16u};
    copy_to(data, buff.prepared_area());
    buff.commit(14u);
    const auto* base = buff.committed_area().data();  // start of the allocation
    buff.consume(8u);

    // Requested space (4) doesn't fit in the prepared area (2), but reclaiming the consumed
    // area (8) is enough, so the committed area is memmoved to the front and no reallocation
    // happens. The committed bytes are preserved and the prepared area grows by the consumed size.
    buff.prepare(4u);
    NATIVEPG_TEST_CONT_EQ(buff.committed_area(), std::span(data).last(6));
    BOOST_TEST_EQ(buff.prepared_area().size(), 10u);
    BOOST_TEST_EQ(buff.committed_area().data(), base);  // moved to the front, same allocation
}

// Same, but required space == (consumed + prepared)
void test_prepare_memmoves_required_equals_total()
{
    constexpr unsigned char data[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};

    // Set up: 6 consumed, 6 committed, 4 free
    read_buffer buff{16u};
    copy_to(data, buff.prepared_area());
    buff.commit(12u);
    const auto* base = buff.committed_area().data();  // start of the allocation
    buff.consume(6u);

    // Requested space (10) equals the total free space
    buff.prepare(10u);
    NATIVEPG_TEST_CONT_EQ(buff.committed_area(), std::span(data).last(6));
    BOOST_TEST_EQ(buff.prepared_area().size(), 10u);
    BOOST_TEST_EQ(buff.committed_area().data(), base);  // moved to the front, same allocation
}

// Two consecutive prepares are OK
void test_two_prepares()
{
    constexpr unsigned char data[] = {1, 2, 3, 4, 5, 6, 7, 8};

    // Fill the buffer so the next prepare must reallocate
    read_buffer buff{8u};
    copy_to(data, buff.prepared_area());
    buff.commit(8u);

    // First prepare reallocates: next_power_of_2(8 + 10) == 32, prepared area == 24
    buff.prepare(10u);
    NATIVEPG_TEST_CONT_EQ(buff.committed_area(), data);
    BOOST_TEST_EQ(buff.prepared_area().size(), 24u);

    // Second prepare for a size that already fits is a no-op
    buff.prepare(24u);
    NATIVEPG_TEST_CONT_EQ(buff.committed_area(), data);
    BOOST_TEST_EQ(buff.prepared_area().size(), 24u);

    // A larger prepare reallocates again: next_power_of_2(8 + 30) == 64, prepared area == 56
    buff.prepare(30u);
    NATIVEPG_TEST_CONT_EQ(buff.committed_area(), data);
    BOOST_TEST_EQ(buff.prepared_area().size(), 56u);
}

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

    test_consume_entire_committed_area();
    test_consume_over_committed_area();
    test_consume_zero();
    test_two_consumes();

    test_prepare_below_prepared_size();
    test_prepare_zero();
    test_prepare_reallocates_empty();
    test_prepare_reallocates_with_committed();
    test_prepare_reallocates_drops_consumed();
    test_prepare_reallocates_consumed_and_committed();
    test_prepare_memmoves();
    test_prepare_memmoves_required_equals_total();
    test_two_prepares();

    return boost::report_errors();
}
