//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/assert.hpp>
#include <boost/assert/source_location.hpp>
#include <boost/capy/buffers.hpp>
#include <boost/capy/buffers/make_buffer.hpp>
#include <boost/capy/error.hpp>
#include <boost/capy/io_task.hpp>
#include <boost/capy/task.hpp>
#include <boost/core/lightweight_test.hpp>
#include <boost/describe/class.hpp>
#include <boost/describe/operators.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "nativepg/co_connection.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/request.hpp"
#include "nativepg/responses/into.hpp"
#include "test_utils/co_connection_utils.hpp"
#include "test_utils/corosio_utils.hpp"
#include "test_utils/printing.hpp"
#include "test_utils/test_range_eq.hpp"

// Covers co_connection::copy_in

namespace capy = boost::capy;
using namespace nativepg;
using namespace nativepg::test;
using namespace std::string_view_literals;

namespace {

struct row_copy
{
    int id;
    std::string name;
};
BOOST_DESCRIBE_STRUCT(row_copy, (), (id, name))

using boost::describe::operators::operator==;
using boost::describe::operators::operator<<;

// A BufferSource that hands the data over in chunks, one chunk per pull()
class chunk_source
{
    std::span<const std::string_view> chunks_;
    std::size_t consumed_{};  // bytes consumed from the current chunk

public:
    explicit chunk_source(std::span<const std::string_view> chunks) noexcept : chunks_(chunks) {}

    capy::io_task<std::span<capy::const_buffer>> pull(std::span<capy::const_buffer> dest)
    {
        // Discard chunks that have been fully consumed
        while (!chunks_.empty() && consumed_ == chunks_.front().size())
        {
            chunks_ = chunks_.subspan(1u);
            consumed_ = 0u;
        }

        if (chunks_.empty())
            co_return {capy::error::eof, {}};

        dest[0] = capy::make_buffer(chunks_.front().substr(consumed_));
        co_return {std::error_code(), dest.first(1u)};
    }

    void consume(std::size_t n) noexcept
    {
        BOOST_ASSERT(!chunks_.empty());
        BOOST_ASSERT(consumed_ + n <= chunks_.front().size());
        consumed_ += n;
    }
};

// TODO: duplicated
capy::task<> check_copy_rows(
    co_connection& conn,
    std::span<const row_copy> expected,
    boost::source_location loc = BOOST_CURRENT_LOCATION
)
{
    std::vector<row_copy> actual;
    request req_check;
    req_check.add_query("SELECT id, name FROM copy_in_test ORDER BY id");
    if (!co_await checked_exec(conn, req_check, into(actual), loc))
        co_return;
    test_range_eq(actual, expected, loc);
}

// The source is drained in two separate pull() calls,
// and all the rows reach the server
capy::task<> test_success()
{
    // Setup
    auto conn = co_await establish_connection();

    if (!co_await checked_exec(conn, "CREATE TEMPORARY TABLE copy_in_test (id INT, name TEXT)"))
        co_return;

    // Run the copy. The source splits the rows into two chunks
    request req;
    req.add_query("COPY copy_in_test FROM STDIN");
    const std::string_view chunks[] = {"1\tone\n2\ttwo\n"sv, "3\tthree\n"sv};
    chunk_source source{chunks};

    diagnostics diag;
    auto [ec, rows] = co_await conn.copy_in(req, &source, &diag);
    if (!check_success(ec, diag))
        co_return;

    // The server reports how many rows it copied
    BOOST_TEST_EQ(rows, std::uint64_t(3));

    // The rows made it into the table
    const row_copy expected[] = {
        {.id = 1, .name = "one"  },
        {.id = 2, .name = "two"  },
        {.id = 3, .name = "three"},
    };
    co_await check_copy_rows(conn, expected);

    co_await check_connection_usable(conn);
}

}  // namespace

int main()
{
    run_coroutine_test(test_success());

    return boost::report_errors();
}
