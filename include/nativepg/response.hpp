//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_RESPONSE_HPP
#define NATIVEPG_RESPONSE_HPP

#include <boost/mp11/algorithm.hpp>
#include <boost/system/error_code.hpp>
#include <boost/variant2/variant.hpp>

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "nativepg/client_errc.hpp"
#include "nativepg/detail/field_traits.hpp"
#include "nativepg/detail/row_traits.hpp"
#include "nativepg/dynamic_resultset.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/field_view.hpp"
#include "nativepg/protocol/bind.hpp"
#include "nativepg/protocol/command_complete.hpp"
#include "nativepg/protocol/command_complete_tag.hpp"
#include "nativepg/protocol/data_row.hpp"
#include "nativepg/protocol/describe.hpp"
#include "nativepg/protocol/execute.hpp"
#include "nativepg/protocol/notice_error.hpp"
#include "nativepg/protocol/parse.hpp"
#include "nativepg/protocol/views.hpp"
#include "nativepg/request.hpp"
#include "nativepg/response_handler.hpp"
#include "nativepg/sqlstate.hpp"

// TODO: we need to split this file

namespace nativepg {

namespace detail {

struct pos_map_entry
{
    // Index within the fields sent by the DB
    std::size_t db_index;

    // Metadata required to parse the field
    protocol::field_description descr;
};

// TODO: string diagnostic
boost::system::error_code compute_pos_map(
    const protocol::row_description& meta,
    std::span<const std::string_view> name_table,
    std::span<pos_map_entry> output
);

inline constexpr std::size_t invalid_pos = static_cast<std::size_t>(-1);

handler_setup_result resultset_setup(const request& req, std::size_t offset);

inline void maybe_store_error(const protocol::error_response& err, extended_error& to)
{
    if (!to.code)
    {
        to.code = parse_sqlstate(err.sqlstate.value_or(std::string_view{}));
        to.diag.assign(err);
    }
}

inline void maybe_store_error(const any_request_message& msg, extended_error& to)
{
    const auto* err = boost::variant2::get_if<protocol::error_response>(&msg);
    if (err)
        maybe_store_error(*err, to);
}

}  // namespace detail

// Information about the execution of a command included in the
// CommandComplete / PortalSuspended
struct command_info
{
    // The tag in the CommandComplete message. Akin to PQcmdStatus
    std::string command_complete_tag{};

    // The rows affected by the command. Only available for a subset of commands.
    // Shouldn't be taken for granted. Akin to PQcmdStatus
    std::optional<std::uint64_t> affected_rows{};

    // True if the max row count specified in an execute message was reached.
    bool portal_suspended{false};

    void reset()
    {
        command_complete_tag.clear();
        affected_rows.reset();
        portal_suspended = false;
    }
};

// Handles a resultset (i.e. a row_description + data_rows + command_complete)
// by invoking a user-supplied callback
template <class T, std::invocable<T&&> Callback>
class resultset_callback_t
{
    enum class state_t
    {
        parsing_meta,
        parsing_data,
        done,
    };

    state_t state_{state_t::parsing_meta};
    std::array<detail::pos_map_entry, detail::row_size_v<T>> pos_map_;
    std::vector<field_view> random_access_data_;
    extended_error err_;
    Callback cb_;
    command_info* info_{};

    void store_error(boost::system::error_code ec)
    {
        if (!err_.code)
        {
            err_.code = ec;
            err_.diag = {};
        }
    }

    struct visitor
    {
        resultset_callback_t& self;

        // We shouldn't get any unexpected messages
        template <class Msg>
        void operator()(const Msg&) const
        {
            self.store_error(client_errc::incompatible_response_type);  // just in case
            BOOST_ASSERT(false);
        }

        // If the server sends an error, store it.
        // We know this is the last message in the sequence.
        void operator()(const protocol::error_response& err) const
        {
            detail::maybe_store_error(err, self.err_);
        }

        // Ignore messages that may or may not appear
        void operator()(protocol::parse_complete) const {}
        void operator()(protocol::bind_complete) const {}

        // Metadata
        void operator()(const protocol::row_description& msg) const
        {
            // State check
            // TODO: this can trigger on multi-queries
            BOOST_ASSERT(self.state_ == state_t::parsing_meta);

            // We now expect the rows and the CommandComplete
            self.state_ = state_t::parsing_data;

            // Compute the row => C++ map
            auto ec = detail::compute_pos_map(msg, detail::row_name_table_v<T>, self.pos_map_);
            if (ec)
            {
                self.store_error(ec);
                return;  // we will just ignore rows
            }

            // Metadata check
            using type_identities = boost::mp11::
                mp_transform<std::type_identity, detail::row_field_types_t<T>>;
            std::size_t idx = 0u;
            boost::mp11::mp_for_each<type_identities>(
                [&idx, &ec, &pos_map = self.pos_map_](auto type_identity) {
                    using FieldType = typename decltype(type_identity)::type;
                    auto ec2 = detail::field_is_compatible<FieldType>::call(pos_map[idx++].descr);
                    if (!ec)
                        ec = ec2;
                }
            );
            if (ec)
            {
                self.store_error(ec);
                return;
            }
        }

        void operator()(const protocol::data_row& msg) const
        {
            // State check
            BOOST_ASSERT(self.state_ == state_t::parsing_data);

            // If there was a previous failure, the field descriptions may not be present and
            // it's not safe to parse. We still need to get to the CommandComplete message
            if (self.err_.code)
                return;

            // TODO: check that data_row has the appropriate size

            // Copy the pointers to the data that we will be using to a random access collection
            self.random_access_data_.assign(msg.columns.begin(), msg.columns.end());

            // Now invoke parse
            T row{};
            boost::system::error_code ec;
            std::size_t idx = 0u;
            detail::for_each_member(row, [&ec, &idx, &self = this->self](auto& member) {
                using FieldType = std::decay_t<decltype(member)>;
                const detail::pos_map_entry& ent = self.pos_map_[idx++];
                boost::system::error_code ec2 = detail::field_parse<FieldType>::call(
                    self.random_access_data_.at(ent.db_index),
                    ent.descr,
                    member
                );
                if (!ec)
                    ec = ec2;
            });
            if (ec)
            {
                self.store_error(ec);
                return;
            }

            // Invoke the user-supplied callback
            self.cb_(std::move(row));

            // We still need the CommandComplete message
        }

        void on_done() const
        {
            // State check
            BOOST_ASSERT(self.state_ == state_t::parsing_data);

            // Done
            self.state_ = state_t::done;
        }

        void operator()(protocol::command_complete msg) const
        {
            if (auto* info = self.info_)
            {
                info->command_complete_tag.assign(msg.tag);

                // Parsing the command tag is best effort. The protocol does not
                // enforce the format. Extensions and third party tools may send non-conforming tags,
                // and the format may change in slightly incompatible ways in the future.
                // libpq does the same.
                auto ec = protocol::parse_command_complete_tag(msg.tag, info->affected_rows);
                static_cast<void>(ec);
            }
            on_done();
        }

        void operator()(protocol::portal_suspended) const
        {
            if (auto* info = self.info_)
                info->portal_suspended = true;
            on_done();
        }

        // If any of the messages we expect was skipped due to a previous error,
        // that's an error
        void operator()(message_skipped) const { self.store_error(client_errc::step_skipped); }
    };

public:
    template <std::invocable<T&&> Cb>
    explicit resultset_callback_t(Cb&& cb, command_info* out_info = nullptr)
        : cb_(std::forward<Cb>(cb)), info_(out_info)
    {
    }

    handler_setup_result setup(const request& req, std::size_t offset)
    {
        state_ = state_t::parsing_meta;
        err_ = {};
        if (info_)
            info_->reset();
        return detail::resultset_setup(req, offset);
    }

    void on_message(const any_request_message& msg, std::size_t)
    {
        boost::variant2::visit(visitor{*this}, msg);
    }

    const extended_error& result() const { return err_; }
};

// Helper to create resultset callbacks
template <class T, std::invocable<T&&> Callback>
auto resultset_callback(Callback&& cb)
{
    return resultset_callback_t<T, std::decay_t<Callback>>{std::forward<Callback>(cb)};
}

namespace detail {

template <class T>
struct into_handler
{
    std::vector<T>& vec;
    void operator()(T&& r) const { vec.push_back(std::move(r)); }
};

}  // namespace detail

// Resultset callback that output rows into a vector
// TODO: other allocators
template <class T>
resultset_callback_t<T, detail::into_handler<T>> into(std::vector<T>& vec, command_info* out_info = nullptr)
{
    return resultset_callback_t<T, detail::into_handler<T>>{
        detail::into_handler<T>{vec, out_info}
    };
}

// A response type that checks that no operation resulted in an error
class check
{
    extended_error err_;

public:
    check() = default;
    handler_setup_result setup(const request& req, std::size_t)
    {
        err_ = {};
        return req.messages().size();
    }
    void on_message(const any_request_message& msg, std::size_t) { detail::maybe_store_error(msg, err_); }
    const extended_error& result() const { return err_; }
};

// A response type that checks that a single resultset didn't
// produce an error, skipping any produced data
class check_execute
{
    extended_error err_;

public:
    check_execute() = default;

    handler_setup_result setup(const request& req, std::size_t offset)
    {
        err_ = {};
        return detail::resultset_setup(req, offset);
    }
    void on_message(const any_request_message& msg, std::size_t) { detail::maybe_store_error(msg, err_); }
    const extended_error& result() const { return err_; }
};

// A response that checks that a single parse (e.g. when preparing a statement)
// didn't produce an error
class check_parse
{
    extended_error err_;

public:
    check_parse() = default;

    handler_setup_result setup(const request& req, std::size_t offset);
    void on_message(const any_request_message& msg, std::size_t) { detail::maybe_store_error(msg, err_); }
    const extended_error& result() const { return err_; }
};

// A response that checks that a single parse (e.g. when preparing a statement)
// didn't produce an error
class check_bind
{
    extended_error err_;

public:
    check_bind() = default;

    handler_setup_result setup(const request& req, std::size_t offset);
    void on_message(const any_request_message& msg, std::size_t) { detail::maybe_store_error(msg, err_); }
    const extended_error& result() const { return err_; }
};

// A response that checks that a single close didn't produce an error
class check_close
{
    extended_error err_;

public:
    check_close() = default;

    handler_setup_result setup(const request& req, std::size_t offset);
    void on_message(const any_request_message& msg, std::size_t) { detail::maybe_store_error(msg, err_); }
    const extended_error& result() const { return err_; }
};

// A response that reads data into a dynamic_resultset
class dynamic_resultset_response
{
    enum class state_t
    {
        parsing_meta,
        parsing_data,
        done,
    };

    dynamic_resultset* obj_;
    extended_error err_;
    state_t state_{state_t::parsing_meta};

public:
    explicit dynamic_resultset_response(dynamic_resultset& obj) noexcept : obj_(&obj) {}

    handler_setup_result setup(const request& req, std::size_t offset)
    {
        obj_->clear();
        state_ = state_t::parsing_meta;
        err_ = {};
        return detail::resultset_setup(req, offset);
    }
    void on_message(const any_request_message& msg, std::size_t);
    const extended_error& result() const { return err_; }
};

namespace detail {

template <class H0, class... HRest>
handler_setup_result response_setup(
    const request& req,
    std::size_t initial_offset,
    std::size_t* out_offsets,
    H0& h0,
    HRest&... hrest
)
{
    // Setup the first handler
    handler_setup_result h0_res = h0.setup(req, initial_offset);

    // Exit on failure
    if (h0_res.ec)
        return h0_res;

    // Store the offset
    *out_offsets = h0_res.offset;

    // Prevent infinite recursion
    if constexpr (sizeof...(HRest) == 0u)
    {
        return h0_res.offset;
    }
    else
    {
        // Recursively setup the rest of the handlers
        return response_setup(req, h0_res.offset, out_offsets + 1, hrest...);
    }
}

template <class H0, class... HRest>
const extended_error* response_get_result(const H0& h0, const HRest&... hrest)
{
    // Get the result for the first handler
    const extended_error& err = h0.result();

    // If it is an error, return this one
    if (err.code)
        return &err;

    // Prevent infinite recursion
    if constexpr (sizeof...(HRest) == 0u)
    {
        return nullptr;
    }
    else
    {
        // Recursively look for the other handlers
        return response_get_result(hrest...);
    }
}

}  // namespace detail

template <response_handler... Handlers>
class response
{
    static inline constexpr std::size_t N = sizeof...(Handlers);

    std::tuple<Handlers...> handlers_;
    std::array<std::size_t, N> offsets_{};
    std::size_t current_{};

public:
    template <class... Args>
        requires std::constructible_from<decltype(handlers_), Args&&...>
    explicit response(Args&&... args) : handlers_(std::forward<Args>(args)...)
    {
    }

    handler_setup_result setup(const request& req, std::size_t offset)
    {
        return std::apply(
            [this, &req, offset](auto&... h) {
                return detail::response_setup(req, offset, offsets_.data(), h...);
            },
            handlers_
        );
    }

    void on_message(const any_request_message& msg, std::size_t offset)
    {
        // Advance to the next element, if required
        if (offset >= offsets_[current_])
            ++current_;
        BOOST_ASSERT(offset < offsets_[current_]);

        // Hand the message to the appropriate handler
        boost::mp11::mp_with_index<N>(current_, [this, &msg, offset](auto I) {
            std::get<I>(handlers_).on_message(msg, offset);
        });
    }

    const extended_error& result() const
    {
        static_assert(N > 0);
        const auto* res = std::apply(
            [](const auto&... h) { return detail::response_get_result(h...); },
            handlers_
        );
        return res != nullptr ? *res : std::get<0>(handlers_).result();
    }

    const auto& handlers() const& { return handlers_; }
    auto& handlers() & { return handlers_; }
    auto&& handlers() && { return std::move(handlers_); }
};

template <class... Args>
response(Args&&...) -> response<std::decay_t<Args>...>;

}  // namespace nativepg

#endif
