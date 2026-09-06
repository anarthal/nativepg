//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/assert.hpp>
#include <boost/endian/conversion.hpp>
#include <boost/throw_exception.hpp>

#include <algorithm>
#include <cstring>
#include <span>
#include <stdexcept>
#include <system_error>
#include <vector>

#include "nativepg/client_errc.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/request.hpp"
#include "nativepg/responses/check.hpp"
#include "nativepg/responses/describe_into.hpp"
#include "nativepg/responses/field_descriptions.hpp"
#include "nativepg/responses/response_handler.hpp"
#include "nativepg/responses/resultset_callback.hpp"
#include "nativepg/responses/resultsets.hpp"
#include "nativepg/responses/resultsets_handler.hpp"

using namespace nativepg;
using namespace nativepg::types;

void nativepg::any_request_message::throw_invalid_argument()
{
    BOOST_THROW_EXCEPTION(std::invalid_argument("any_request_message: kind mismatch"));
}

static constexpr std::size_t invalid_pos = static_cast<std::size_t>(-1);

std::error_code nativepg::detail::compute_pos_map(
    const protocol::row_description& meta,
    std::span<const std::string_view> name_table,
    std::span<pos_map_entry> output
)
{
    // Name table should be the same size as the pos map
    BOOST_ASSERT(name_table.size() == output.size());

    // Set all positions to "invalid"
    for (auto& elm : output)
        elm = {invalid_pos, {}, {}};

    // Look up every DB field in the name table
    std::size_t db_index = 0u;
    for (const auto& field : meta.field_descriptions)
    {
        auto it = std::find(name_table.begin(), name_table.end(), field.name);
        if (it != name_table.end())
        {
            auto cpp_index = static_cast<std::size_t>(it - name_table.begin());
            output[cpp_index] = {db_index, field.type_oid, field.fmt_code};
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
    return check_setup_impl(req, offset, request_message_type::parse);
}

handler_setup_result check_close::setup(const request& req, std::size_t offset)
{
    return check_setup_impl(req, offset, request_message_type::close);
}

handler_setup_result describe_into::setup(const request& req, std::size_t offset)
{
    obj_->clear();
    return check_setup_impl(req, offset, request_message_type::describe);
}

void check_execute::on_message(const any_request_message& msg, std::size_t, extended_error& err)
{
    using kind = any_request_message::kind;

    switch (msg.type())
    {
        // Ignore messages that might or might not appear in exec
        case kind::bind_complete:
        case kind::parse_complete: break;

        // Ignore metadata
        case kind::row_description: break;

        // Ignore any data
        case kind::data_row: break;

        // EOF
        case kind::command_complete:
            if (info_)
                detail::from_command_complete(*info_, msg.get_command_complete());
            break;

        case kind::portal_suspended:
            if (info_)
                info_->portal_suspended = true;
            break;

        // Errors
        case kind::error_response: detail::store_error(msg.get_error_response(), err); break;

        // The rest of the messages shouldn't arrive
        // TODO: manage multi-queries, empty queries, skipped messages
        default: BOOST_ASSERT(false); break;
    }
}

handler_setup_result resultsets_handler::setup(const request& req, std::size_t offset)
{
    obj_->clear();
    reset_state();

    auto res = detail::resultset_setup(req, offset);
    while (true)
    {
        if (res.ec || res.offset >= req.messages().size())
            return res;
        res = detail::resultset_setup(req, res.offset);
    }
}

void resultsets_handler::on_message(const any_request_message& msg, std::size_t, extended_error& err)
{
    using kind = any_request_message::kind;

    switch (msg.type())
    {
        // Ignore messages that might or might not appear in exec
        case kind::bind_complete:
        case kind::parse_complete: break;

        // Metadata
        case kind::row_description:
        {
            const auto& descr = msg.get_row_description();
            BOOST_ASSERT(state_ == state_t::parsing_meta);
            num_cols_ = descr.field_descriptions.size();
            obj_->add_row_description(descr);
            state_ = state_t::parsing_data;
            break;
        }

        // Data
        case kind::data_row:
            BOOST_ASSERT(state_ == state_t::parsing_data);
            // TODO: check that the number of rows matches with what we received in the field description
            obj_->add_row(msg.get_data_row());
            ++num_rows_;
            break;

        // EOF
        case kind::command_complete:
        {
            BOOST_ASSERT(state_ == state_t::parsing_data);
            command_info info;
            detail::from_command_complete(info, msg.get_command_complete());
            obj_->finish_resultset(num_rows_, num_cols_, std::move(info), {});
            reset_state();
            break;
        }

        case kind::portal_suspended:
            BOOST_ASSERT(state_ == state_t::parsing_data);
            obj_->finish_resultset(num_rows_, num_cols_, {.portal_suspended = true}, {});
            reset_state();
            break;

        // Errors
        case kind::error_response:
        {
            extended_error err_temp;
            detail::store_error(msg.get_error_response(), err_temp);
            err = err_temp;
            obj_->finish_resultset(num_rows_, num_cols_, {}, std::move(err_temp));
            reset_state();
            break;
        }

        // The rest of the messages shouldn't arrive
        // TODO: manage multi-queries, empty queries, skipped messages
        default: BOOST_ASSERT(false); break;
    }
}

void describe_into::on_message(const any_request_message& msg, std::size_t, extended_error& err)
{
    using kind = any_request_message::kind;

    switch (msg.type())
    {
        // The row description is the result of a describe (portal or statement).
        // A no_data reply is delivered by the FSM as an empty row description.
        case kind::row_description: obj_->assign(msg.get_row_description()); break;

        // A describe statement is preceded by a parameter description, which we don't store
        case kind::parameter_description: break;

        // Errors
        case kind::error_response: detail::store_error(msg.get_error_response(), err); break;

        // We only handle describe messages, so nothing else should arrive
        default: BOOST_ASSERT(false); break;
    }
}

static nativepg::detail::offset_and_length insert_data(
    std::vector<unsigned char>& to,
    std::span<const unsigned char> value
)
{
    // Data coming from the server fulfills this assertion by protocol design
    BOOST_ASSERT(value.size() != static_cast<std::size_t>(-1));
    nativepg::detail::offset_and_length res{.offset = to.size(), .length = value.size()};
    to.insert(to.end(), value.begin(), value.end());
    return res;
}

static nativepg::detail::offset_and_length insert_data(std::vector<unsigned char>& to, std::string_view value)
{
    return insert_data(
        to,
        std::span<const unsigned char>{reinterpret_cast<const unsigned char*>(value.data()), value.size()}
    );
}

void field_descriptions::assign(const protocol::row_description& row_descr)
{
    clear();
    field_descr_.reserve(row_descr.field_descriptions.size());
    for (const auto& descr : row_descr.field_descriptions)
    {
        field_descr_.push_back({
            .name = insert_data(data_, descr.name),
            .table_oid = descr.table_oid,
            .column_attribute = descr.column_attribute,
            .type_oid = descr.type_oid,
            .type_length = descr.type_length,
            .type_modifier = descr.type_modifier,
            .fmt_code = descr.fmt_code,
        });
    }
}

void resultsets::add_row_description(const protocol::row_description& row_descr)
{
    field_descr_.reserve(field_descr_.size() + row_descr.field_descriptions.size());
    for (const auto& descr : row_descr.field_descriptions)
    {
        field_descr_.push_back({
            .name = insert_data(data_, descr.name),
            .table_oid = descr.table_oid,
            .column_attribute = descr.column_attribute,
            .type_oid = descr.type_oid,
            .type_length = descr.type_length,
            .type_modifier = descr.type_modifier,
            .fmt_code = descr.fmt_code,
        });
    }
}

// Part of the unstable API. Should only be used by
// response authors.
void resultsets::add_row(const protocol::data_row& row)
{
    values_.reserve(row.columns.size());
    for (const auto fv : row.columns)
    {
        if (fv.is_null())
            values_.push_back({.offset = 0u, .length = static_cast<std::size_t>(-1)});
        else
            values_.push_back(insert_data(data_, fv.data()));
    }
}

void resultsets::finish_resultset(
    std::size_t num_rows,
    std::size_t num_cols,
    command_info&& info,
    extended_error&& err
)
{
    const std::size_t num_values = num_cols * num_rows;

    BOOST_ASSERT(field_descr_.size() >= num_cols);
    BOOST_ASSERT(values_.size() >= num_values);

    resultsets_.push_back({
        .err = std::move(err),
        .info = std::move(info),
        .descr = {.offset = field_descr_.size() - num_cols, .length = num_cols  },
        .values = {.offset = values_.size() - num_values,    .length = num_values},
    });
}
