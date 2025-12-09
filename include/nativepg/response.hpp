//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_RESPONSE_HPP
#define NATIVEPG_RESPONSE_HPP

#include <boost/core/span.hpp>
#include <boost/mp11/algorithm.hpp>
#include <boost/system/error_code.hpp>
#include <boost/variant2/variant.hpp>

#include <array>
#include <concepts>
#include <cstddef>
#include <functional>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>

#include "nativepg/client_errc.hpp"
#include "nativepg/protocol/bind.hpp"
#include "nativepg/protocol/close.hpp"
#include "nativepg/protocol/command_complete.hpp"
#include "nativepg/protocol/data_row.hpp"
#include "nativepg/protocol/describe.hpp"
#include "nativepg/protocol/empty_query_response.hpp"
#include "nativepg/protocol/execute.hpp"
#include "nativepg/protocol/notice_error.hpp"
#include "nativepg/protocol/parse.hpp"
#include "nativepg/row_traits.hpp"

namespace nativepg {

// TODO: maybe make this a class
using any_request_message = boost::variant2::variant<
    protocol::bind_complete,
    protocol::close_complete,
    protocol::command_complete,
    protocol::data_row,
    protocol::parameter_description,
    protocol::row_description,
    protocol::no_data,
    protocol::empty_query_response,
    protocol::portal_suspended,
    protocol::error_response,
    protocol::notice_response,
    protocol::parse_complete>;

class vector_response
{
    using handler_type = std::function<boost::system::error_code(const any_request_message&)>;

    vector_response() = default;

    void add(handler_type h) { handlers_.push_back(std::move(h)); }

private:
    std::vector<handler_type> handlers_;
};

// TODO: field traits, TODO: string diagnostics
template <class T>
boost::system::error_code field_is_compatible(const protocol::field_description& desc);

// TODO: implement
template <class T>
boost::system::error_code field_parse(
    std::optional<boost::span<const unsigned char>> from,
    const protocol::field_description& desc,
    T& to
);

// TODO: string diagnostic
boost::system::error_code compute_pos_map(
    const protocol::row_description& meta,
    std::span<const std::string_view> name_table,
    std::vector<std::size_t> output
);

template <class T, std::invocable<T&&> Callback>
class resultset_callback
{
    enum class state_t
    {
        parsing_meta,
        parsing_data,
        done,
    };

    state_t state_{state_t::parsing_meta};
    std::vector<std::size_t> pos_map_;
    std::array<protocol::field_description, detail::row_size_v<T>> descriptions_;
    std::array<std::string_view, detail::row_size_v<T>> random_access_data_;
    Callback cb_;

    struct visitor
    {
        resultset_callback& self;

        boost::system::error_code operator()(const protocol::row_description& msg) const
        {
            // State check
            if (self.state_ != state_t::parsing_meta)
                return client_errc::unexpected_message;

            // Compute the row => C++ map
            auto ec = compute_pos_map(msg, detail::row_name_table_v<T>, self.pos_map_);
            if (ec)
                return ec;

            // Store the metadata required for parsing
            std::size_t i = 0u;
            for (auto it = msg.field_descriptions.begin(); it != msg.field_descriptions.end(); ++i, ++it)
            {
                std::size_t cpp_idx = self.pos_map_.at(i);
                if (cpp_idx != static_cast<std::size_t>(-1))
                {
                    protocol::field_description& desc = self.descriptions_.at(cpp_idx);
                    desc = (*it);
                    desc.name = {};  // names not retained, as it would require another allocation
                }
            }

            // Metadata check
            using type_identities = boost::mp11::
                mp_transform<std::type_identity, detail::row_field_types_t<T>>;
            std::size_t idx = 0u;
            boost::mp11::mp_for_each<type_identities>(
                [&idx, &ec, &descriptions = self.descriptions_](auto type_identity) {
                    using FieldType = typename decltype(type_identity)::type;
                    auto ec2 = field_is_compatible<FieldType>(descriptions[idx++]);
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

            // Select the data that we will be using
            std::array<std::optional<boost::span<const unsigned char>>, detail::row_size_v<T>> ordered_data;
            std::size_t i = 0u;
            for (auto it = msg.columns.begin(); it != msg.columns.end(); ++it, ++i)
            {
                const std::size_t cpp_idx = self.pos_map_.at(i);
                if (cpp_idx != static_cast<std::size_t>(-1))
                {
                    ordered_data.at(cpp_idx) = (*it);
                }
            }

            // Now invoke parse
            T row{};
            boost::system::error_code ec;
            std::size_t idx = 0u;
            for_each_member(row, [&ec, &idx, &ordered_data, &descs = this->self.descriptions_](auto& member) {
                boost::system::error_code ec2 = field_parse(ordered_data[idx], descs[idx], member);
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
    };

public:
    boost::system::error_code operator()(const any_request_message& msg)
    {
        return boost::variant2::visit(visitor{*this}, msg);
    }
};

}  // namespace nativepg

#endif
