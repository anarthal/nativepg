//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_RESPONSE_HPP
#define NATIVEPG_RESPONSE_HPP

#include <boost/compat/function_ref.hpp>
#include <boost/core/span.hpp>
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

    struct visitor
    {
        resultset_callback_t& self;
        diagnostics& diag;

        // Error on unexpected messages
        template <class Msg>
        boost::system::error_code operator()(const Msg&) const
        {
            return client_errc::unexpected_message;
        }

        // On error, fail the entire operation.
        // TODO: this must be reviewed
        boost::system::error_code operator()(const protocol::error_response& err) const
        {
            diag.assign(err);
            return client_errc::exec_server_error;
        }

        // Ignore messages that may or may not appear
        boost::system::error_code operator()(protocol::parse_complete) const
        {
            // Only allowed before metadata
            return self.state_ == state_t::parsing_meta ? client_errc::needs_more
                                                        : client_errc::unexpected_message;
        }
        boost::system::error_code operator()(protocol::bind_complete) const
        {
            return self.state_ == state_t::parsing_meta ? client_errc::needs_more
                                                        : client_errc::unexpected_message;
        }

        boost::system::error_code operator()(const protocol::row_description& msg) const
        {
            // State check
            if (self.state_ != state_t::parsing_meta)
                return client_errc::unexpected_message;

            // Compute the row => C++ map
            auto ec = detail::compute_pos_map(msg, detail::row_name_table_v<T>, self.pos_map_);
            if (ec)
                return ec;

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
                return ec;

            // We now expect the rows and the CommandComplete
            self.state_ = state_t::parsing_data;
            return client_errc::needs_more;
        }

        boost::system::error_code operator()(protocol::no_data) const
        {
            return (*this)(protocol::row_description{});
        }

        boost::system::error_code operator()(const protocol::data_row& msg) const
        {
            // State check
            if (self.state_ != state_t::parsing_data)
                return client_errc::unexpected_message;

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
                return ec;

            // Invoke the user-supplied callback
            self.cb_(std::move(row));

            // We still need the CommandComplete message
            return client_errc::needs_more;
        }

        boost::system::error_code operator()(protocol::command_complete) const
        {
            // State check
            if (self.state_ != state_t::parsing_data)
                return client_errc::unexpected_message;

            // Done
            self.state_ = state_t::done;
            return {};
        }

        // TODO: this should be transmitted to the user somehow
        boost::system::error_code operator()(protocol::portal_suspended) const
        {
            // State check
            if (self.state_ != state_t::parsing_data)
                return client_errc::unexpected_message;

            // Done
            self.state_ = state_t::done;
            return {};
        }
    };

public:
    template <class Cb>
    explicit resultset_callback_t(Cb&& cb) : cb_(std::forward<Cb>(cb))
    {
    }

    boost::system::error_code operator()(const any_request_message& msg, diagnostics& diag)
    {
        auto ec = boost::variant2::visit(visitor{*this, diag}, msg);
        if (ec)
        {
            // Store the error
            err_.code = ec;
            err_.diag = diag;
        }
        return ec;
    }

    const extended_error& error() const { return err_; }
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
    std::size_t current_{};

public:
    template <class... Args>
        requires std::constructible_from<decltype(handlers_), Args&&...>
    explicit response(Args&&... args)
        : handlers_(std::forward<Args>(args)...),
          vtable_(std::apply([](auto&... h) { return std::array<response_handler_ref, N>{h...}; }, handlers_))
    {
    }

    // TODO: implement move, at least
    response(response&&) = delete;
    response& operator=(response&&) = delete;

    boost::system::error_code operator()(const any_request_message& msg, diagnostics& diag)
    {
        // If we're done and another message is received, that's an error
        if (current_ >= N)
            return client_errc::unexpected_message;

        // Call the handler
        auto ec = vtable_[current_](msg, diag);

        if (ec)
        {
            // Both errors and needs_more should just be propagated
            return ec;
        }
        else
        {
            // This handler is done
            if (++current_ == N)
            {
                // All handlers are done
                return {};
            }
            else
            {
                // There are still more handlers that need messages
                return client_errc::needs_more;
            }
        }
    }

    const auto& handlers() const& { return handlers_; }
    auto& handlers() & { return handlers_; }
    auto&& handlers() && { return std::move(handlers_); }
};

template <class... Args>
response(Args&&...) -> response<std::decay_t<Args>...>;

}  // namespace nativepg

#endif
