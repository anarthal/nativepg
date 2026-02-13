//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_RESPONSE_HPP
#define NATIVEPG_RESPONSE_HPP

#include <boost/core/span.hpp>
#include <boost/describe/class.hpp>
#include <boost/mp11/algorithm.hpp>
#include <boost/system/error_code.hpp>
#include <boost/variant2/variant.hpp>

#include <array>
#include <concepts>
#include <cstddef>
#include <optional>
#include <span>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "nativepg/client_errc.hpp"
#include "nativepg/detail/field_traits.hpp"
#include "nativepg/detail/row_traits.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/protocol/bind.hpp"
#include "nativepg/protocol/command_complete.hpp"
#include "nativepg/protocol/data_row.hpp"
#include "nativepg/protocol/describe.hpp"
#include "nativepg/protocol/execute.hpp"
#include "nativepg/protocol/notice_error.hpp"
#include "nativepg/protocol/parse.hpp"
#include "nativepg/protocol/views.hpp"
#include "nativepg/request.hpp"
#include "nativepg/response_handler.hpp"

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

struct empty
{
};
BOOST_DESCRIBE_STRUCT(empty, (), ());

inline std::vector<empty> empty_vec{};

}  // namespace detail

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
    std::vector<std::optional<std::span<const unsigned char>>> random_access_data_;
    extended_error err_;
    Callback cb_;

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
            if (!self.err_.code)
            {
                self.err_.code = client_errc::exec_server_error;
                self.err_.diag.assign(err);
            }
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

        void operator()(protocol::command_complete) const { on_done(); }

        // TODO: this should be transmitted to the user somehow
        void operator()(protocol::portal_suspended) const { on_done(); }

        // If any of the messages we expect was skipped due to a previous error,
        // that's an error
        void operator()(message_skipped) const { self.store_error(client_errc::step_skipped); }
    };

public:
    template <class Cb>
    explicit resultset_callback_t(Cb&& cb) : cb_(std::forward<Cb>(cb))
    {
    }

    handler_setup_result setup(const request& req, std::size_t offset)
    {
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
resultset_callback_t<T, detail::into_handler<T>> into(std::vector<T>& vec)
{
    return resultset_callback_t<T, detail::into_handler<T>>{detail::into_handler<T>{vec}};
}

template <response_handler... Handlers>
class response
{
    static inline constexpr std::size_t N = sizeof...(Handlers);

    std::tuple<Handlers...> handlers_;
    std::array<response_handler_ref, N> vtable_;
    std::array<std::size_t, N> offsets_{};
    std::size_t current_{};

public:
    template <class... Args>
        requires std::constructible_from<decltype(handlers_), Args&&...>
    explicit response(Args&&... args)
        : handlers_(std::forward<Args>(args)...),
          vtable_(std::apply([](auto&... h) { return std::array<response_handler_ref, N>{h...}; }, handlers_))
    {
    }

    // move constructor
    response(response&& other) noexcept
        :   handlers_(std::move(other.handlers_)),
            vtable_(std::move(other.vtable_)),
            offsets_(std::move(other.offsets_)),
            current_(other.current_)
    {}

    // move assignment
    response& operator=(response&& other) noexcept {
        handlers_ = std::move(other.handlers_);
        vtable_ = std::move(other.vtable_);
        offsets_ = std::move(other.offsets_);
        current_ = other.current_;
        return *this;
    }

    handler_setup_result setup(const request& req, std::size_t offset)
    {
        for (std::size_t i = 0u; i < N; ++i)
        {
            handler_setup_result res = vtable_[i].setup(req, offset);
            if (res.ec)
                return res;
            offsets_[i] = res.offset;
            offset = res.offset;
        }
        return {offset};
    }

    void on_message(const any_request_message& msg, std::size_t offset)
    {
        // Advance to the next element, if required
        if (offset >= offsets_[current_])
            ++current_;
        BOOST_ASSERT(offset < offsets_[current_]);

        // Hand the message to the appropriate handler
        vtable_[current_].on_message(msg, offset);
    }

    const extended_error& result() const
    {
        static_assert(N > 0);
        for (auto& elm : vtable_)
        {
            const auto& res = elm.result();
            if (res.code)
                return res;
        }
        return vtable_[0].result();
    }

    const auto& handlers() const& { return handlers_; }
    auto& handlers() & { return handlers_; }
    auto&& handlers() && { return std::move(handlers_); }
};

template <class... Args>
response(Args&&...) -> response<std::decay_t<Args>...>;


struct errors_only_handler
{
    extended_error err;
    handler_setup_result setup(const request& req, std::size_t offset) { return detail::resultset_setup(req, offset); }
    void on_message(const any_request_message&, std::size_t) {}
    const extended_error& result() const { return err; }
};

const errors_only_handler eo{};
inline response eo_response{eo};

}  // namespace nativepg

#endif
