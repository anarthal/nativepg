//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_COPY_OUT_HPP
#define NATIVEPG_COPY_OUT_HPP

#include <boost/capy/buffers.hpp>
#include <boost/capy/buffers/buffer_copy.hpp>
#include <boost/capy/buffers/make_buffer.hpp>
#include <boost/capy/io/any_write_sink.hpp>
#include <boost/capy/io_task.hpp>
#include <boost/capy/when_all.hpp>
#include <boost/container/small_vector.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <system_error>

#include "nativepg/client_errc.hpp"
#include "nativepg/co_connection.hpp"
#include "nativepg/exclusivity.hpp"
#include "nativepg/exec_state.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/protocol/command_complete_tag.hpp"
#include "nativepg/request.hpp"
#include "nativepg/responses/any_request_message.hpp"
#include "nativepg/responses/detail/response_utils.hpp"
#include "nativepg/responses/response_handler.hpp"

namespace nativepg::detail {

// Type used to hold Copy-out buffers
using copy_out_buffers = boost::container::small_vector<boost::capy::const_buffer, 32u>;

// Response handler used by copy_out
class copy_out_handler
{
    struct
    {
        std::uint64_t rows_{};
        std::size_t num_copies_{};
        bool in_copy_{};
    } state_;

    copy_out_buffers* buffs_;

public:
    copy_out_handler(copy_out_buffers& buffs) noexcept : buffs_(&buffs) {}

    handler_setup_result setup(const request& req, std::size_t)
    {
        // The request should contain at least one Query/Execute
        const bool may_copy = std::ranges::any_of(req.messages(), [](request_message_type type) {
            return type == request_message_type::execute || type == request_message_type::query;
        });
        if (!may_copy)
            return make_error_code(client_errc::incompatible_response_type);

        // No need to clear state, because this handler is never re-used
        return req.messages().size();
    }

    void on_message(const any_request_message& msg, std::size_t, extended_error& err)
    {
        switch (msg.type())
        {
            case any_request_message::kind::copy_out_response:
                ++state_.num_copies_;
                state_.in_copy_ = true;
                break;
            case any_request_message::kind::copy_data:
            {
                // If the request triggers several Copy-out's, this hands incorrect
                // data to the sink. But this is OK because we never signal EOF.
                auto data = msg.get_copy_data().data;
                buffs_->emplace_back(data.data(), data.size());
                break;
            }
            case any_request_message::kind::command_complete:
                // The tag that closes a copy carries the number of copied rows
                // Parsing it is best-effort, as the protocol doesn't make guarantees about its format
                if (state_.in_copy_)
                {
                    std::optional<std::uint64_t> opt_rows;
                    static_cast<void>(
                        protocol::parse_command_complete_tag(msg.get_command_complete().tag, opt_rows)
                    );
                    state_.rows_ = opt_rows.value_or(0u);
                    state_.in_copy_ = false;
                }
                break;
            case any_request_message::kind::error_response:
                detail::store_error(msg.get_error_response(), err);
                state_.in_copy_ = false;
                break;
            default: break;
        }
    }

    // How many times did the server start a copy?
    std::size_t num_copies() const { return state_.num_copies_; }

    // Rows reported by the CommandComplete that closed the copy
    std::uint64_t rows() const { return state_.rows_; }
};

// Reader side of copy_out
inline boost::capy::io_task<> copy_out_reader(
    co_connection& conn,
    exec_state& st,
    copy_out_buffers& buffs,
    boost::capy::any_write_sink& sink
)
{
    while (true)
    {
        // Read a batch of messages. This invokes the handler, which collects
        // whatever copy data the batch contained
        if (auto [ec] = co_await conn.read_some_response(st); ec)
            co_return {ec};

        // We've read the response in full, and no longer hold the reader turn.
        // Because the sink is async, we need to copy the contents of the last
        // buffers (we no longer hold the read turn, so other requests might invalidate the buffer).
        // TODO: I don't like this. This is a smell. Probably we should replace
        //   exclusivity by something like 'manual turns', where the user explicitly
        //   tells us that we can keep reading/writing.
        if (st.read_done())
        {
            auto tail_size = boost::capy::buffer_size(buffs);
            if (tail_size > 0u)
            {
                std::unique_ptr<unsigned char[]> tail_storage{new unsigned char[tail_size]};
                boost::capy::mutable_buffer tail_buff{tail_storage.get(), tail_size};
                boost::capy::buffer_copy(tail_buff, buffs);
                if (auto [ec, bytes] = co_await sink.write(boost::capy::const_buffer(tail_buff)); ec)
                    co_return {ec};
            }
            co_return {};
        }

        // Hand the batch to the sink. Empty batches are a no-op
        // No lifetime problems here because we still own the read-side.
        if (auto [ec, bytes] = co_await sink.write(std::span<const boost::capy::const_buffer>(buffs)); ec)
            co_return {ec};

        buffs.clear();
    }
}

inline boost::capy::io_task<std::uint64_t> copy_out(
    co_connection& conn,
    const request& req,
    boost::capy::any_write_sink sink,
    diagnostics* diag
)
{
    // Clear diagnostics
    if (diag)
        *diag = diagnostics();

    // Setup
    copy_out_buffers buffs;
    copy_out_handler handler(buffs);
    exec_state st;
    if (auto ec = conn.prepare_request(st, req, &handler, exclusivity::shared))
        co_return {ec, 0u};

    // Run the reader and the writer in parallel. Running them in
    // sequence may deadlock for big requests (neither the server not us
    // read because we are waiting for space in the write buffer).
    // Reports transport and sink errors.
    auto [final_ec, writer_dummy, reader_dummy] = co_await boost::capy::when_all(
        conn.write_request(st),
        copy_out_reader(conn, st, buffs, sink)
    );
    if (final_ec)
        co_return {final_ec, 0u};

    // Did the server report any error?
    // This includes the case where the server errors the request
    // and never enters Copy-out mode
    if (st.handler_error().code)
    {
        if (diag)
            *diag = st.handler_error().diag;
        co_return {st.handler_error().code, 0u};
    }

    // Did the request start exactly one Copy-out?
    if (handler.num_copies() != 1u)
        co_return {client_errc::incompatible_response_type, 0u};

    // Inform the sink that we're done. We do this here
    // because we want it done on success only.
    if (auto [ec] = co_await sink.write_eof(); ec)
        co_return {ec, 0u};

    co_return {std::error_code(), handler.rows()};
}

}  // namespace nativepg::detail

#endif
