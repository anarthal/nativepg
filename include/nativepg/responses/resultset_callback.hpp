//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_RESULTSET_CALLBACK_HPP
#define NATIVEPG_RESULTSET_CALLBACK_HPP

#include <boost/assert.hpp>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "nativepg/detail/row_traits.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/field_traits.hpp"
#include "nativepg/field_view.hpp"
#include "nativepg/protocol/describe.hpp"
#include "nativepg/responses/command_info.hpp"
#include "nativepg/responses/detail/response_utils.hpp"

namespace nativepg {

namespace detail {

struct pos_map_entry
{
    // Index within the fields sent by the DB
    std::size_t db_index;

    // The OID of the field's type
    std::int32_t type_oid;

    // The format the DB uses to send the field
    protocol::format_code fmt_code;
};

// TODO: string diagnostic
std::error_code compute_pos_map(
    const protocol::row_description& meta,
    std::span<const std::string_view> name_table,
    std::span<pos_map_entry> output
);

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
        failed,
    };

    state_t state_{state_t::parsing_meta};
    std::array<detail::pos_map_entry, detail::row_size_v<T>> pos_map_;
    std::vector<field_view> random_access_data_;
    Callback cb_;
    command_info* info_{};

    struct visitor
    {
        resultset_callback_t& self;
        extended_error& out_err;

        // We shouldn't get any unexpected messages
        template <class Msg>
        void operator()(const Msg&) const
        {
            out_err.code = client_errc::incompatible_response_type;  // just in case
            BOOST_ASSERT(false);
        }

        // If the server sends an error, store it.
        // We know this is the last message in the sequence.
        void operator()(const protocol::error_response& err) const { detail::store_error(err, out_err); }

        // Ignore messages that may or may not appear
        void operator()(protocol::parse_complete) const {}
        void operator()(protocol::bind_complete) const {}

        // Metadata
        void operator()(const protocol::row_description& msg) const
        {
            // State check
            // TODO: this can trigger on multi-queries
            BOOST_ASSERT(self.state_ == state_t::parsing_meta);

            // Compute the row => C++ map
            if (auto ec = detail::compute_pos_map(msg, detail::row_name_table_v<T>, self.pos_map_))
            {
                out_err.code = ec;
                self.state_ = state_t::failed;
                return;  // we will just ignore rows
            }

            // Metadata check
            using type_identities = boost::mp11::
                mp_transform<std::type_identity, detail::row_field_types_t<T>>;
            std::size_t idx = 0u;
            std::error_code ec;
            boost::mp11::mp_for_each<type_identities>(
                [&idx, &ec, &pos_map = self.pos_map_](auto type_identity) {
                    using FieldType = typename decltype(type_identity)::type;
                    auto ec2 = field_is_compatible<FieldType>(pos_map[idx++].type_oid);
                    if (!ec)
                        ec = ec2;
                }
            );
            if (ec)
            {
                self.state_ = state_t::failed;
                out_err.code = ec;
                return;
            }

            // We now expect the rows and the CommandComplete
            self.state_ = state_t::parsing_data;
        }

        void operator()(const protocol::data_row& msg) const
        {
            // State check
            // If there was a previous failure, the field descriptions may not be present and
            // it's not safe to parse. We still need to get to the CommandComplete message
            if (self.state_ == state_t::failed)
                return;
            BOOST_ASSERT(self.state_ == state_t::parsing_data);

            // TODO: check that data_row has the appropriate size

            // Copy the pointers to the data that we will be using to a random access collection
            self.random_access_data_.assign(msg.columns.begin(), msg.columns.end());

            // Now invoke parse
            T row{};
            std::error_code ec;
            std::size_t idx = 0u;
            detail::for_each_member(row, [&ec, &idx, &self = this->self](auto& member) {
                const detail::pos_map_entry& ent = self.pos_map_[idx++];
                const field_view fv = self.random_access_data_.at(ent.db_index);
                std::error_code ec2 = field_parse(fv, ent.type_oid, ent.fmt_code, member);
                if (!ec)
                    ec = ec2;
            });
            if (ec)
            {
                out_err.code = ec;
                return;
            }

            // Invoke the user-supplied callback
            self.cb_(std::move(row));

            // We still need the CommandComplete message
        }

        void operator()(protocol::command_complete msg) const
        {
            // State check
            if (self.state_ == state_t::failed)
                return;
            BOOST_ASSERT(self.state_ == state_t::parsing_data);

            // Store info
            if (auto* info = self.info_)
                detail::from_command_complete(*info, msg);

            // Update state
            self.state_ = state_t::done;
        }

        void operator()(protocol::portal_suspended) const
        {
            // State check
            if (self.state_ == state_t::failed)
                return;
            BOOST_ASSERT(self.state_ == state_t::parsing_data);

            // Store info
            if (auto* info = self.info_)
                info->portal_suspended = true;

            // Update state
            self.state_ = state_t::done;
        }

        // If any of the messages we expect was skipped due to a previous error,
        // that's an error
        void operator()(message_skipped) const { out_err.code = client_errc::step_skipped; }
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
        if (info_)
            detail::reset_info(*info_);
        return detail::resultset_setup(req, offset);
    }

    void on_message(const any_request_message& msg, std::size_t, extended_error& err)
    {
        boost::variant2::visit(visitor{*this, err}, msg);
    }
};

// Helper to create resultset callbacks
template <class T, std::invocable<T&&> Callback>
auto resultset_callback(Callback&& cb, command_info* info = nullptr)
{
    return resultset_callback_t<T, std::decay_t<Callback>>{std::forward<Callback>(cb), info};
}

}  // namespace nativepg

#endif
