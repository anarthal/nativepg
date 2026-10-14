//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/capy/buffers.hpp>
#include <boost/capy/concept/write_sink.hpp>
#include <boost/capy/ex/immediate.hpp>
#include <boost/capy/ex/run_async.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/write.hpp>
#include <boost/corosio/file_base.hpp>
#include <boost/corosio/io_context.hpp>
#include <boost/corosio/stream_file.hpp>

#include <iostream>
#include <utility>

#include "nativepg/co_connection.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/request.hpp"

using namespace nativepg;
namespace capy = boost::capy;
namespace corosio = boost::corosio;

namespace {

class file_sink
{
    corosio::stream_file file_;

public:
    explicit file_sink(corosio::stream_file f) noexcept : file_(std::move(f)) {}

    template <capy::ConstBufferSequence Buffers>
    auto write_some(Buffers&& buffs)
    {
        return file_.write_some(std::forward<Buffers>(buffs));
    }

    template <capy::ConstBufferSequence Buffers>
    auto write(Buffers&& buffs)
    {
        return capy::write(file_, std::forward<Buffers>(buffs));
    }

    template <capy::ConstBufferSequence Buffers>
    auto write_eof(Buffers&& buffs)
    {
        return capy::write(file_, std::forward<Buffers>(buffs));
    }

    auto write_eof() { return capy::ready(); }
};

static_assert(capy::WriteSink<file_sink>);

}  // namespace

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

    // Open the file to write to
    corosio::stream_file file(co_await capy::this_coro::executor);
    file.open(
        "/tmp/data.tsv",
        corosio::file_base::write_only | corosio::file_base::create | corosio::file_base::truncate
    );

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
    req.add_query("COPY myt TO STDOUT");

    // Run the copy
    if (auto [ec, nrows] = co_await conn.copy_out(req, file_sink(std::move(file)), &diag); ec)
    {
        print_err("Error in COPY OUT", ec, diag);
        co_return;
    }

    // Orderly close the connection.
    // May report a failure if the server is gone.
    // The connection is closed anyway in that case.
    if (auto [shutdown_ec] = co_await conn.shutdown(); shutdown_ec)
    {
        print_err("Error during shutdown", shutdown_ec, {});
        co_return;
    }
}

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
