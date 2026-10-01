//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_DETAIL_EXEC_STATE_IMPL_HPP
#define NATIVEPG_DETAIL_EXEC_STATE_IMPL_HPP

#include <boost/capy/ex/async_event.hpp>
#include <boost/intrusive/list_hook.hpp>

#include <cstddef>
#include <optional>
#include <span>

#include "nativepg/protocol/read_response_fsm.hpp"
#include "nativepg/request.hpp"
#include "nativepg/responses/response_handler.hpp"

namespace nativepg::detail {

class multiplexer_v2;

// All the state for a single exec operation
struct exec_state_impl : boost::intrusive::list_base_hook<>
{
    enum class writer_status
    {
        // We haven't acquired the write mutex yet
        initial,

        // We hold the write mutex and still owe an exit report.
        // A partially written request stays here, so it can be resumed
        locked,

        // The writer exited and was accounted for
        done,
    };

    // The multiplexer we're registered with, or nullptr if we were never set up
    multiplexer_v2* mpx{};

    // Where does the writer stand?
    writer_status writer_st{writer_status::initial};

    // Did the reader report its exit?
    bool reader_done{};

    // How many bytes of our request's payload reached the server.
    // A write that fails half-way leaves this at the resume point
    std::size_t bytes_written{};

    // Is there a write_request operation in flight for this state?
    bool writing{};

    // Is there a read_some_response operation in flight for this state?
    bool reading{};

    // Number of ReadyForQuery messages that we expect from
    // previously cancelled items
    std::size_t pending_rfqs{};

    // How many ReadyForQuery messages did the reader read?
    // This includes RFQs from leftover requests before us.
    // -1 means "I've read everything I was supposed to and have no leftover"
    std::size_t read_rfqs{};

    // Setting it notifies the task to read next
    boost::capy::async_event evt{};

    // Tracks the response as it is read. TODO: optional not good
    std::optional<protocol::read_response_fsm> fsm;

    // Did the user call prepare_request()?
    bool is_prepared() const { return mpx != nullptr; }

    // Did each of the two halves of the operation finish?
    bool write_done() const { return writer_st == writer_status::done; }
    bool read_done() const { return reader_done; }

    // Did the writer write at least one byte of our request?
    bool request_committed() const { return bytes_written > 0u; };

    // Releases anything we still hold in the multiplexer and returns
    // to a pristine state. Defined in multiplexer_v2.hpp
    void reset();

    // Cleans up any leftover from a previous operation and prepares for a new one.
    // The request and the handler must outlive the operation.
    // Defined in multiplexer_v2.hpp
    void setup(multiplexer_v2& mpx, const request& req, response_handler_ref handler);

    const request& get_request() const { return fsm->get_request(); }
    std::span<const unsigned char> remaining_payload() const
    {
        return get_request().payload().subspan(bytes_written);
    }
};

}  // namespace nativepg::detail

#endif
