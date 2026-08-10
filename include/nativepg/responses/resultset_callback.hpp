//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_RESULTSET_CALLBACK_HPP
#define NATIVEPG_RESULTSET_CALLBACK_HPP

#include <boost/assert.hpp>
#include <boost/container/small_vector.hpp>

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <tuple>

#include "nativepg/extended_error.hpp"
#include "nativepg/field_traits.hpp"
#include "nativepg/field_view.hpp"
#include "nativepg/member_descriptor.hpp"
#include "nativepg/protocol/common.hpp"
#include "nativepg/protocol/describe.hpp"
#include "nativepg/responses/command_info.hpp"
#include "nativepg/responses/detail/response_utils.hpp"

namespace nativepg {

namespace detail {

using field_parse_cb = std::error_code (*)(field_view, std::int32_t, protocol::format_code, void*);
using field_is_compatible_cb = std::error_code (*)(std::int32_t);

// A function suitable for parse_field_cb
template <class Row, class FieldType, FieldType Row::* member>
std::error_code do_field_parse(field_view fv, std::int32_t type_oid, protocol::format_code code, void* row)
{
    return field_parse(fv, type_oid, code, static_cast<Row*>(row)->*member);
}

// Describes a C++ field. An array of these describes a C++ struct
struct erased_member_descriptor
{
    // The field name to look up in the DB query
    std::string_view field_name;

    // Checks for compatibility
    field_is_compatible_cb is_compatible_fn;

    // Parses a field against the row struct
    field_parse_cb parse_fn;
};

template <class Row, class FieldType, FieldType Row::* member>
constexpr erased_member_descriptor erase_descriptor(member_descriptor<Row, FieldType, member> desc)
{
    return {
        .field_name = desc.name,
        .is_compatible_fn = field_is_compatible<FieldType>,
        .parse_fn = do_field_parse<Row, FieldType, member>
    };
}

template <class Row, class... Descriptors>
constexpr std::array<erased_member_descriptor, sizeof...(Descriptors)> erase_descriptors(
    std::tuple<Descriptors...> descs
)
{
    return std::apply([](auto... desc) { return std::array(erase_descriptor(desc)...); }, descs);
}

struct mapper_entry
{
    field_parse_cb parse_fn;
    std::int32_t type_oid;
    protocol::format_code code;
};

// TODO: string diagnostic
std::error_code metadata_check(
    const protocol::row_description& meta,
    std::span<const erased_member_descriptor> cpp_descriptors,
    std::span<mapper_entry> output
);

}  // namespace detail

// Handles a resultset (i.e. a row_description + data_rows + command_complete)
// by invoking a user-supplied callback
template <class T, class DescriptorTupleTag, std::invocable<T&&> Callback>
class resultset_callback_t
{
    enum class state_t
    {
        parsing_meta,
        parsing_data,
        done,
        failed,
    };

    static inline constexpr auto descriptors = DescriptorTupleTag::get();
    static inline constexpr std::size_t row_size = std::tuple_size_v<decltype(descriptors)>;

    state_t state_{state_t::parsing_meta};
    boost::container::small_vector<detail::mapper_entry, row_size * 5 / 4> mapper_;
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

            // Compute the query => C++ map
            self.mapper_.resize(msg.field_descriptions.size());
            if (auto ec = detail::metadata_check(msg, descriptors, self.mapper_))
            {
                out_err.code = ec;
                self.state_ = state_t::failed;
                return;  // we will just ignore rows
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

            // Now invoke parse
            T row{};
            std::size_t i = 0u;
            for (auto fv : msg.columns)
            {
                const detail::mapper_entry& entry = self.mapper_.at(i);
                if (entry.parse_fn)
                {
                    // The field is mapped
                    if (auto ec = entry.parse_fn(fv, entry.type_oid, entry.code, &row))
                    {
                        out_err.code = ec;
                        return;
                    }
                }

                ++i;
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
