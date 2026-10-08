//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/assert.hpp>
#include <boost/capy/buffers.hpp>
#include <boost/capy/buffers/make_buffer.hpp>
#include <boost/capy/concept/buffer_source.hpp>
#include <boost/capy/concept/executor.hpp>
#include <boost/capy/cond.hpp>
#include <boost/capy/error.hpp>
#include <boost/capy/ex/async_event.hpp>
#include <boost/capy/ex/execution_context.hpp>
#include <boost/capy/ex/run_async.hpp>
#include <boost/capy/ex/this_coro.hpp>
#include <boost/capy/io_task.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/when_all.hpp>
#include <boost/capy/write.hpp>
#include <boost/corosio/file_base.hpp>
#include <boost/corosio/io_context.hpp>
#include <boost/corosio/stream_file.hpp>
#include <boost/describe/class.hpp>

#include <concepts>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>

#include "nativepg/co_connection.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/request.hpp"

using namespace nativepg;
namespace capy = boost::capy;
namespace corosio = boost::corosio;

namespace {

// A capy::BufferSource that yields the entire contents of a file,
// reading it in chunks as the consumer requests more data.
class file_source
{
    corosio::stream_file file_;
    std::vector<unsigned char> buffer_;
    std::size_t size_{};    // number of valid bytes in buffer_
    std::size_t offset_{};  // bytes of buffer_ already consumed, in [0, size_]
    bool eof_{};            // did the file report end-of-file?

public:
    // Bytes read from the file per I/O operation, unless overridden
    static constexpr std::size_t default_buffer_size = 64u * 1024u;

    // Opens path for reading. Throws std::system_error on failure
    file_source(
        capy::execution_context& ctx,
        const std::filesystem::path& path,
        std::size_t buffer_size = default_buffer_size
    )
        : file_(ctx), buffer_(buffer_size)
    {
        BOOST_ASSERT(buffer_size > 0u);
        file_.open(path, corosio::file_base::read_only);
    }

    // Same, but taking an executor instead of a context
    template <class Ex>
        requires(!std::same_as<std::remove_cvref_t<Ex>, file_source>) && capy::Executor<Ex>
    file_source(
        const Ex& ex,
        const std::filesystem::path& path,
        std::size_t buffer_size = default_buffer_size
    )
        : file_source(ex.context(), path, buffer_size)
    {
    }

    // Returns the bytes that haven't been consumed yet, reading from the file
    // if required. Reports capy::cond::eof once the file has been exhausted.
    // dest must not be empty; at most one descriptor is written to it
    capy::io_task<std::span<capy::const_buffer>> pull(std::span<capy::const_buffer> dest)
    {
        BOOST_ASSERT(!dest.empty());

        // Only hit the file when everything we read last time has been consumed.
        // Otherwise, hand out the remainder again, as the concept requires
        if (offset_ == size_)
        {
            if (eof_)
                co_return {capy::error::eof, {}};

            offset_ = 0u;
            size_ = 0u;

            // A successful pull must yield data, so keep reading until we get some
            while (size_ == 0u)
            {
                auto [ec, bytes] = co_await file_.read_some(
                    capy::mutable_buffer(buffer_.data(), buffer_.size())
                );
                if (ec == capy::cond::eof)
                {
                    eof_ = true;
                    co_return {capy::error::eof, {}};
                }
                if (ec)
                    co_return {ec, {}};
                size_ = bytes;
            }
        }

        // The memory stays valid until the next pull/consume, as required
        dest[0] = capy::const_buffer(buffer_.data() + offset_, size_ - offset_);
        co_return {{}, dest.first(1)};
    }

    // Discards n bytes from the data returned by the last pull
    void consume(std::size_t n) noexcept
    {
        BOOST_ASSERT(n <= size_ - offset_);
        offset_ += n;
    }
};

static_assert(capy::BufferSource<file_source>);

static void print_err(const char* prefix, std::error_code err, const diagnostics& diag)
{
    std::cout << prefix << ": " << err << ": " << err.message();
    if (!diag.message().empty())
        std::cout << ": " << diag.message();
    std::cout << '\n';
}

static capy::task<> co_main()
{
    // Create a connection
    co_connection conn{co_await capy::this_coro::executor};
    diagnostics diag;

    // Connect
    if (auto [ec] = co_await conn.connect(
            {.hostname = "localhost", .username = "postgres", .password = "secret", .database = "postgres"},
            &diag
        );
        ec)
    {
        print_err("Error connecting", ec, diag);
        co_return;
    }
    std::cout << "Startup complete\n";

    // Compose our request
    request req;
    req.add_query("COPY myt FROM STDIN");

    // Execute the copy operation
    auto [ec, num_rows] = co_await conn.copy_in(
        req,
        file_source(co_await capy::this_coro::executor, "/tmp/data.tsv"),
        &diag
    );
    if (ec)
    {
        print_err("Error executing the Copy", ec, diag);
        co_return;
    }

    std::cout << "Successfully copied " << num_rows << " rows\n";

    // Orderly close the connection.
    // May report a failure if the server is gone.
    // The connection is closed anyway in that case.
    if (auto [shutdown_ec] = co_await conn.shutdown(); shutdown_ec)
    {
        print_err("Error during shutdown", shutdown_ec, {});
        co_return;
    }
}

}  // namespace

int main()
{
    // The I/O context, required for all I/O operations
    corosio::io_context ctx;

    // Schedules the main coroutine for execution
    capy::run_async(
        ctx.get_executor(),
        []() {
           // Runs when the main coroutine finishes normally
           std::cout << "Done\n";
        },
        [](std::exception_ptr exc) {
            // Runs when the main coroutine finishes with an exception
            try {
               std::rethrow_exception(exc);
            } catch (const std::exception& e) {
               std::cerr << "Error: " << e.what() << std::endl;
            }
            exit(1);
        }
    )(co_main());

    // Executes all pending work, including the main coroutine
    ctx.run();
}
