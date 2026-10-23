//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_CO_CONNECTION_HPP
#define NATIVEPG_CO_CONNECTION_HPP

#include <boost/capy/buffers/buffer_param.hpp>
#include <boost/capy/concept/const_buffer_sequence.hpp>
#include <boost/capy/concept/executor.hpp>
#include <boost/capy/ex/execution_context.hpp>
#include <boost/capy/io/any_stream.hpp>
#include <boost/capy/results.hpp>
#include <boost/capy/task.hpp>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <system_error>

#include "nativepg/capy-stub/io/any_buffer_source.hpp"
#include "nativepg/capy-stub/io/any_write_sink.hpp"
#include "nativepg/connect_params.hpp"
#include "nativepg/encoding.hpp"
#include "nativepg/exclusivity.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/protocol/connection_state.hpp"
#include "nativepg/protocol/ready_for_query.hpp"
#include "nativepg/request.hpp"
#include "nativepg/responses/response_handler.hpp"

namespace nativepg {

class exec_state;
class notification_vector;

namespace detail {
struct co_connection_impl;
}

class co_connection
{
    std::unique_ptr<detail::co_connection_impl> impl_;

public:
    explicit co_connection(boost::capy::execution_context& ctx);

    template <class Ex>
        requires(!std::same_as<Ex, co_connection> && boost::capy::Executor<Ex>)
    explicit co_connection(const Ex& ex) : co_connection{ex.context()}
    {
    }

    co_connection(co_connection&&) noexcept;
    co_connection(const co_connection&) = delete;

    co_connection& operator=(co_connection&&) noexcept;
    co_connection& operator=(const co_connection&) = delete;

    // Destroys the connection. If any operation is in-flight, the behavior is undefined.
    ~co_connection();

    // Establishes a connection to the server.
    // If the connection is established (is_connected() == true), tears down the current
    //   session before establishing the new one. The session is closed abruptly: no Terminate message
    //   is sent, and the TLS shutdown is skipped. Any operation in flight is cancelled,
    //   finishing with an ec == capy::cond::canceled. Call shutdown() first if you want
    //   a graceful close.
    // After a successful completion, is_connected() == true, and last_error() is empty.
    // After a completion with an error, is_connected() == false, and last_error() contains
    //   the error that caused the failure. This holds even if a session was established
    //   when connect() was called.
    // Notifications that were received but not read with receive() are discarded.
    // Failure modes:
    //   - If another connect() or a shutdown() is outstanding, fails with already_running.
    boost::capy::task<boost::capy::results<>> connect(connect_params params, diagnostics* diag = nullptr);

    // Closes the connection (PQfinish).
    // This is a coroutine because shutting down requires communication with the server
    //   (sending a Terminate message, and invoking TLS shutdown, if applicable).
    // The behavior depends on the connection's state:
    //   - If it's established, no other operation is in-flight, and no exec_state
    //     has been started but not finished, performs a clean shutdown: sends a Terminate
    //     message and closes the underlying transport.
    //   - Otherwise, it skips the Terminate message, and just closes the transport.
    // Any in-flight operation finishes with an ec == capy::cond::canceled.
    // exec_state's that haven't been run until completion are marked as abandoned,
    //   with successive operations on them returning ec == capy::cond::canceled.
    // If an error is encountered while attempting the clean shutdown, returns it.
    // In any case, after this function completes, is_connected() == false,
    //   and last_error() is empty.
    boost::capy::task<boost::capy::results<>> shutdown();

    boost::capy::task<boost::capy::results<>> exec(
        const request& req,
        response_handler_ref handler,
        diagnostics* diag = nullptr
    );

    template <response_handler ResponseHandler>
    boost::capy::task<boost::capy::results<>> exec(
        const request& req,
        ResponseHandler handler,
        diagnostics* diag = nullptr
    )
    {
        // Keep the handler alive
        co_return co_await exec(req, response_handler_ref(&handler), diag);
    }

    // Waits until either a notification arrives, or an error occurs.
    // The connection must be in established state.
    // Received notifications are stored in output, which is cleared first.
    // Can be called in parallel with other exec() operations.
    // Only one receive() operation is allowed to be in-flight at any given time.
    //   Issuing another fails with client_errc::already_running.
    // If at least one notification is read, returns a non-empty error code.
    //   If an error condition is detected after some notifications have been read,
    //   they are returned, and no error is reported.
    // If notifications arrive during a session but are not read with receive(),
    //   and then a re-connection happens (e.g. shutdown + connect),
    //   cached notifications are discarded.
    // Failure modes:
    //   - If the connection hasn't been established, fails with not_connected.
    boost::capy::task<boost::capy::results<>> receive(notification_vector& output);

    // Runs a request containing a single COPY ... FROM STDIN, feeding the server
    //   with the data produced by source.
    // The request is executed using exclusivity::exclusive. Requests issued while
    //   the operation is in-flight will wait until it completes.
    // The request must trigger copy-in exactly once. Otherwise, it
    //   fails with client_errc::incompatible_response_type.
    // source is pulled until it reports EOF, and the obtained data is transferred
    //   to the server. Data is never inspected - you should make sure that its
    //   format matches the one expected by the server.
    // The request and diagnostics objects must be kept alive until the operation
    //   completes. If source is non-owning, the object pointed-to by it should
    //   be kept alive, too.
    // On success, returns the number of rows that the server reports having copied.
    // Failure modes:
    //   - If the server rejects the request or the data we send (e.g. the target table
    //     doesn't exist, or a row has an invalid format), the server's error is reported,
    //     and diag is populated. Sending data stops as soon as the error is noticed.
    //     The connection is left usable.
    //   - If source fails, the transfer is aborted by sending a CopyFail containing
    //     the error's message, and the error reported by source is returned.
    //     The connection is left usable.
    //   - If the connection hasn't been established, fails with not_connected.
    //   - If the transport fails, the relevant error is returned. The connection
    //     is left in an unspecified state and should be closed.
    // Cancelling this operation leaves the connection in an unusable state.
    //   Close the connection if this happens.
    boost::capy::task<boost::capy::results<std::uint64_t>> copy_in(
        const request& req,
        boost::capy::any_buffer_source source,
        diagnostics* diag = nullptr
    );

    // Runs a request containing a single COPY ... TO STDOUT, transferring the data
    //   produced by the server to sink.
    // The request is executed using exclusivity::shared, since Copy-out requires no
    //   messages from the client. It may run in parallel with other requests.
    // The request must trigger copy-out exactly once. Otherwise, it
    //   fails with client_errc::incompatible_response_type.
    // Data is written to sink as it arrives, and is never inspected - you should
    //   make sure that the format produced by the statement is the one sink expects.
    // The request and diagnostics objects must be kept alive until the operation
    //   completes. If sink is non-owning, the object pointed-to by it should
    //   be kept alive, too.
    // On success, sink is finalized by calling write_eof(), and the number of rows
    //   that the server reports having copied is returned.
    // On error, sink is never finalized, and may have been handed
    //   part of the data.
    // Failure modes:
    //   - If the server rejects the request or fails while producing the data
    //     (e.g. the source table doesn't exist), the server's error is reported,
    //     and diag is populated. The connection is left usable.
    //   - If sink fails, the error it reported is returned, and reading stops
    //     immediately. The connection is left usable, but may experience delays,
    //     as any data produced by the Copy-out operation needs to be discarded.
    //     This is performed lazily, when the next operation is executed.
    //   - If the connection hasn't been established, fails with not_connected.
    //   - If the transport fails, the relevant error is returned. The connection
    //     is left in an unspecified state and should be closed.
    //   - Cancelling this operation leaves the connection usable.
    boost::capy::task<boost::capy::results<std::uint64_t>> copy_out(
        const request& req,
        boost::capy::any_write_sink sink,
        diagnostics* diag = nullptr
    );

    // Prepares a request to be executed by the connection.
    // The passed exec_state becomes a handle for the operation that will be executed.
    // Once prepared, the request can be progressed by calling write_request (writer side) and
    //   read_some_response (reader side). Reader and writer may be run in parallel.
    // These functions respect multiplexing, and can be called in parallel with other exec() operations.
    //   If other requests are in-flight, these functions will wait appropriately until the connection is
    //   idle.
    // The exec_state, connection, request, handler and diagnostics must be kept alive
    // until all of its associated operations (reader and writer) complete.
    // If the request hasn't executed fully when the exec_state is destroyed
    //   (i.e. some output generated by the server hasn't been read yet),
    //   subsequent exec() operations will discard that output, and the connection will be kept healthy.
    // Subsequent requests can't make progress until you either read all the output generated by
    //   the server or destroy the exec_state.
    // Returns an error if request and handler are not compatible.
    [[nodiscard]] std::error_code prepare_request(
        exec_state& st,
        const request& req,
        response_handler_ref handler,
        exclusivity excl = exclusivity::shared
    );

    // Writes the request pointed to by st to the server.
    // If another operation is currently writing requests (e.g. another parallel exec()),
    //   waits until its write part finishes before initiating the write.
    // Returns success if all the request's bytes were written to the server.
    //   In this case, st.write_phase() transitions from write_status::request
    //   to write_status::done (for requests using exclusivity::shared)
    //   or write_status::copy_data/write_status::waiting_for_reader (for exclusive requests).
    // If this function finishes with an error, the number of transferred bytes
    //   is stored internally within st. Subsequent write_request calls are allowed.
    //   This allows to resume writing after a cancellation, for example.
    // Failure modes:
    //   - Only one writer function (write_request, write_some_copy_data, write_copy_done or
    //     write_copy_fail) for a given exec_state may be in-flight at a time.
    //     Attempting to launch another fails with client_errc::already_running.
    //   - Requires st.is_prepared() && st.write_phase() == write_status::request.
    //     Otherwise, finishes with client_errc::invalid_state.
    //   - If the connection hasn't been established, fails with not_connected.
    //   - If the exec_state has been abandoned (e.g. because the connection died),
    //     fails with ec == capy::cond::canceled.
    boost::capy::task<boost::capy::results<>> write_request(exec_state& st);

    // Reads part of the response generated by the server and invokes the response handler.
    // On success, reads a batch of messages from the server (unspecified size, at least one)
    //   and invokes the handler as required. Because not all messages involve handler invocation,
    //   read_some_response might invoke the handler zero times and still return success,
    //   meaning that it made progress.
    // If this function finishes with an error (including one matching capy::cond::canceled),
    //   you might call this function again to try to make more progress. The exec_state is updated
    //   as required to track progress.
    // This function is fully independent from the writer functions. You are responsible
    //   for calling write_request(), write_some_copy_data(), write_copy_done() or write_copy_fail()
    //   until st.write_phase() == write_status::done.
    //   A failure in any of the writer functions won't cancel read_some_response(), to allow re-trying.
    // Failure modes:
    //   - Only one instance of read_some_response for a given exec_state is allowed
    //     to be in-flight at a time. Attempting to launch another fails with client_errc::already_running.
    //   - Requires st.is_prepared() && !st.read_done().
    //     Otherwise, finishes with client_errc::invalid_state.
    //   - If the connection hasn't been established, fails with not_connected.
    //   - If the exec_state has been abandoned (e.g. because the connection died),
    //     fails with ec == capy::cond::canceled.
    boost::capy::task<boost::capy::results<>> read_some_response(exec_state& st);

    // Sends the contents of buffers to the server as copy data.
    //   Any memory that buffers points into must be kept alive until the op completes.
    // The data is framed into one or more CopyData messages. Frames are length-prefixed.
    //   The exact framing is unspecified, as it has no meaning for the server.
    // Returns the number of bytes from buffers that reached the server
    //   (framing overhead not included).
    //   As with WriteStream::write_some(), this may be less than buffer_size(buffers) even on
    //   success, so call this function repeatedly until the entire sequence has been consumed.
    // If this function finishes with an ec == capy::cond::canceled,
    //   it may be called again to try to resume the transfer.
    // Once buffers have been handed to this function, they need to be fully written
    //   (by repeatedly calling write_some_copy_data) before the transfer can be finished
    //   with write_copy_done or write_copy_fail. In other words, you cannot "withdraw"
    //   bytes that were handed to this function, even if they were not reported as written.
    //   This is because CopyData messages have a length prefix, written before anything else.
    //   Alternatively, you can reset() or destroy the exec_state and re-open the connection.
    // Failure modes:
    //   - Only one writer function (write_request, write_some_copy_data, write_copy_done or
    //     write_copy_fail) for a given exec_state may be in-flight at a time.
    //     Attempting to launch another fails with client_errc::already_running.
    //   - Requires st.write_phase() == write_status::copy_data.
    //     Otherwise, finishes with client_errc::invalid_state.
    //   - If the exec_state has been abandoned (e.g. because the connection died),
    //     fails with ec == capy::cond::canceled.
    template <boost::capy::ConstBufferSequence ConstBufferSeq>
    boost::capy::task<boost::capy::results<std::size_t>> write_some_copy_data(
        exec_state& st,
        ConstBufferSeq buffers
    )
    {
        // A plain forwarder doesn't work because param needs to be kept alive
        // and is a template
        boost::capy::const_buffer_param<ConstBufferSeq> param{buffers};
        co_return co_await write_some_copy_data_impl(st, param.data());
    }

    // Tells the server that we finished sending copy data successfully (CopyDone).
    // On success, st.write_phase() transitions from write_status::copy_data to
    //   write_status::waiting_for_reader. Read the rest of the response with read_some_response().
    // Requires that all the copy data passed to write_some_copy_data() has been transferred.
    //   Otherwise, finishes with client_errc::copy_incomplete_transfer, and the copy may
    //   still be completed by sending the missing bytes.
    // If this function finishes with an error, the terminator may have been written partially.
    //   Retrying is not supported: the connection should be considered unusable.
    // If the server detects an error in the data (e.g. a row with an invalid format),
    //   it will send an error that can be read with read_some_response.
    //   You should still call write_copy_done or write_copy_fail to terminate the copy
    //   operation, even after reading the error, as this function performs
    //   protocol-level bookkeeping required to keep the connection in sync.
    // Failure modes:
    //   - Only one writer function (write_request, write_some_copy_data, write_copy_done or
    //     write_copy_fail) for a given exec_state may be in-flight at a time.
    //     Attempting to launch another fails with client_errc::already_running.
    //   - Requires st.write_phase() == write_status::copy_data.
    //     Otherwise, finishes with client_errc::invalid_state.
    //   - If the exec_state has been abandoned (e.g. because the connection died),
    //     fails with ec == capy::cond::canceled.
    boost::capy::task<boost::capy::results<>> write_copy_done(exec_state& st);

    // Aborts the copy, making the server discard everything we sent for it (CopyFail).
    // Upon receiving this message, the server exits copy mode and raises an error containing
    //   error_message in it.
    // On success, st.write_phase() transitions from write_status::copy_data to
    //   write_status::waiting_for_reader. Read the rest of the response with read_some_response().
    // Requires that all the copy data passed to write_some_copy_data() has been transferred.
    //   Otherwise, finishes with client_errc::copy_incomplete_transfer, and the copy may
    //   still be completed by sending the missing bytes.
    // If the server detects an error in the data (e.g. a row with an invalid format),
    //   it will send an error that can be read with read_some_response.
    //   You should still call write_copy_done or write_copy_fail to terminate the copy
    //   operation, even after reading the error, as this function performs
    //   protocol-level bookkeeping required to keep the connection in sync.
    // If this function finishes with an error, the terminator may have been written partially.
    //   Retrying is not supported: the connection should be considered unusable.
    // Failure modes:
    //   - Only one writer function (write_request, write_some_copy_data, write_copy_done or
    //     write_copy_fail) for a given exec_state may be in-flight at a time.
    //     Attempting to launch another fails with client_errc::already_running.
    //   - Requires st.write_phase() == write_status::copy_data.
    //     Otherwise, finishes with client_errc::invalid_state.
    //   - If the exec_state has been abandoned (e.g. because the connection died),
    //     fails with ec == capy::cond::canceled.
    boost::capy::task<boost::capy::results<>> write_copy_fail(exec_state& st, std::string_view error_message);

    // Is the connection established? Returns true after
    // a successful call to connect() completes,
    // and until one of the following happens:
    //   - shutdown() initiates
    //   - connect() initiates, replacing the session
    //   - One of the functions requiring server interaction (e.g. exec(), receive())
    //     detects a fatal error (e.g. network failure, protocol violation).
    bool is_connected() const noexcept;

    // Returns the last error encountered by the connection.
    //   - After a call to connect() completes with a failure, returns its error
    //   - After a successful connect() completes, returns empty
    //   - After shutdown() is initiated, returns empty
    //   - After any other function that requires server interaction encounters
    //     a fatal error, returns the encountered error.
    // TODO: make this const extended_error&
    std::error_code last_error() const noexcept;

    // Values reported via ParameterStatus
    std::optional<bool> standard_conforming_strings() const;
    std::optional<encoding> client_encoding() const;

    // The transaction status reported by the last ReadyForQuery message we read.
    // Potential uses include deciding when to issue a ROLLBACK for session re-use.
    // The value is authoritative only when no exec operation is in flight.
    //   Requests that were written but whose response hasn't been read yet may
    //   have already changed the backend's status. If you pipeline requests
    //   (or run exec() operations in parallel), read this after all of them complete.
    // If the connection hasn't been established yet (i.e. `is_connected() == false`),
    //   returns transaction_status::idle.
    // Copy-in and copy-out don't produce a ReadyForQuery until the copy is
    //   terminated, so the value stays at the pre-copy status for their duration.
    protocol::transaction_status txn_status() const noexcept;

    // TODO: this should be removed but is now used within the tests.
    //   Remove once we have accessors for backend PID
    protocol::connection_state& state();

private:
    boost::capy::task<boost::capy::results<std::size_t>> write_some_copy_data_impl(
        exec_state& st,
        std::span<const boost::capy::const_buffer> buffers
    );
};

}  // namespace nativepg

#endif
