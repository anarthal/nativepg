//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/assert.hpp>
#include <boost/endian/conversion.hpp>
#include <boost/system/error_code.hpp>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <span>

#include "nativepg/client_errc.hpp"
#include "nativepg/detail/field_traits.hpp"
#include "nativepg/request.hpp"
#include "nativepg/response.hpp"
#include "nativepg/response_handler.hpp"

using namespace nativepg;
using namespace nativepg::types;
using boost::system::error_code;



boost::system::error_code nativepg::detail::compute_pos_map(
    const protocol::row_description& meta,
    std::span<const std::string_view> name_table,
    std::span<pos_map_entry> output
)
{
    // Name table should be the same size as the pos map
    BOOST_ASSERT(name_table.size() == output.size());

    // Set all positions to "invalid"
    for (auto& elm : output)
        elm = {invalid_pos, {}};

    // Look up every DB field in the name table
    std::size_t db_index = 0u;
    for (const auto& field : meta.field_descriptions)
    {
        auto it = std::find(name_table.begin(), name_table.end(), field.name);
        if (it != name_table.end())
        {
            auto cpp_index = static_cast<std::size_t>(it - name_table.begin());
            output[cpp_index] = {db_index, field};
        }
        ++db_index;
    }

    // If there is any unmapped field, it is an error
    if (std::find_if(output.begin(), output.end(), [](const pos_map_entry& ent) {
            return ent.db_index == invalid_pos;
        }) != output.end())
    {
        return client_errc::field_not_found;
    }

    return {};
}

handler_setup_result nativepg::detail::resultset_setup(const request& req, std::size_t offset)
{
    const auto msgs = req.messages().subspan(offset);
    bool describe_found = false, execute_found = false;
    auto it = msgs.begin();

    // Skip any leading syncs
    while (it != msgs.end() && (*it == request_message_type::sync || *it == request_message_type::flush))
        ++it;

    // The original message may be a query. In this case, it must be the only message
    if (*it == request_message_type::query)
    {
        ++it;
        return {static_cast<std::size_t>(it - req.messages().begin())};
    }

    // Otherwise, it must be an extended query sequence:
    //   optional parse
    //   optional bind
    //   exactly one describe portal
    //   exactly one execute
    // There may be flush messages, but no sync messages in between
    //   (otherwise, error behavior becomes unreliable)
    for (; it != msgs.end() && !execute_found; ++it)
    {
        switch (*it)
        {
            // Ignore parse, bind and flush messages
            case request_message_type::sync: continue;
            case request_message_type::flush:
            case request_message_type::parse:
            case request_message_type::bind: continue;
            case request_message_type::describe:
                if (describe_found)
                    return handler_setup_result(client_errc::incompatible_response_type);
                else
                    describe_found = true;
                break;
            case request_message_type::execute:
                if (!describe_found || execute_found)
                    return handler_setup_result(client_errc::incompatible_response_type);
                else
                    execute_found = true;
                break;
            default: return handler_setup_result(client_errc::incompatible_response_type);
        }
    }

    // Skip any further sync messages
    while (it != msgs.end() && (*it == request_message_type::sync || *it == request_message_type::flush))
        ++it;

    // If we got the execute message, we're good
    return execute_found ? handler_setup_result{static_cast<std::size_t>(it - req.messages().begin())}
                         : handler_setup_result{client_errc::incompatible_response_type};
}

static handler_setup_result check_setup_impl(
    const request& req,
    std::size_t offset,
    request_message_type type
)
{
    const auto msgs = req.messages().subspan(offset);
    auto it = msgs.begin();

    // Skip any leading syncs
    while (it != msgs.end() && (*it == request_message_type::sync || *it == request_message_type::flush))
        ++it;

    // Check that the request contains the message that we expect
    if (it == msgs.end() || *it != type)
        return handler_setup_result(client_errc::incompatible_response_type);
    ++it;

    // Skip any further sync messages
    while (it != msgs.end() && (*it == request_message_type::sync || *it == request_message_type::flush))
        ++it;

    return handler_setup_result{static_cast<std::size_t>(it - req.messages().begin())};
}

handler_setup_result check_parse::setup(const request& req, std::size_t offset)
{
    err_ = {};
    return check_setup_impl(req, offset, request_message_type::parse);
}

handler_setup_result check_bind::setup(const request& req, std::size_t offset)
{
    err_ = {};
    return check_setup_impl(req, offset, request_message_type::bind);
}

handler_setup_result check_close::setup(const request& req, std::size_t offset)
{
    err_ = {};
    return check_setup_impl(req, offset, request_message_type::close);
}

void dynamic_resultset_response::on_message(const any_request_message& msg, std::size_t)
{
    struct visitor
    {
        dynamic_resultset_response& self;

        // Ignore messages that might or might not appear in exec
        void operator()(protocol::bind_complete) const {}
        void operator()(protocol::parse_complete) const {}

        // Metadata
        void operator()(const protocol::row_description& msg) const
        {
            BOOST_ASSERT(self.state_ == state_t::parsing_meta);
            self.obj_->set_row_description(msg);
            self.state_ = state_t::parsing_data;
        }

        // Data
        void operator()(const protocol::data_row& msg) const
        {
            BOOST_ASSERT(self.state_ == state_t::parsing_data);
            // TODO: check that the number of rows matches with what we received in the field description
            self.obj_->add_row(msg);
        }

        // EOF
        void operator()(protocol::command_complete msg) const
        {
            BOOST_ASSERT(self.state_ == state_t::parsing_data);
            self.obj_->set_command_complete_tag(msg.tag);
            self.state_ = state_t::done;
        }

        void operator()(protocol::portal_suspended) const
        {
            BOOST_ASSERT(self.state_ == state_t::parsing_data);
            self.obj_->set_portal_suspended(true);
            self.state_ = state_t::done;
        }

        // Errors
        void operator()(const protocol::error_response& msg) const
        {
            detail::maybe_store_error(msg, self.err_);
            self.state_ = state_t::done;
        }

        // The rest of the messages shouldn't arrive
        // TODO: manage multi-queries, empty queries, skipped messages
        void operator()(const protocol::close_complete&) const { BOOST_ASSERT(false); }
        void operator()(const protocol::parameter_description&) const { BOOST_ASSERT(false); }
        void operator()(const protocol::empty_query_response&) const { BOOST_ASSERT(false); }
        void operator()(message_skipped) const { BOOST_ASSERT(false); }
    };

    boost::variant2::visit(visitor{*this}, msg);
}
