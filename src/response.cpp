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
#include <cstdint>
#include <span>

#include "nativepg/client_errc.hpp"
#include "nativepg/detail/field_traits.hpp"
#include "nativepg/response.hpp"

using namespace nativepg;
using boost::system::error_code;

// Parsing concrete fields

namespace {

template <class T>
error_code parse_text_int(std::span<const unsigned char> from, T& to)
{
    const char* first = reinterpret_cast<const char*>(from.data());
    const char* last = first + from.size();
    auto err = std::from_chars(first, last, to);
    if (err.ec != std::errc{})
        return std::make_error_code(err.ec);
    if (err.ptr != last)
        return client_errc::extra_bytes;
    return error_code();
}

template <class T>
error_code parse_binary_int(std::span<const unsigned char> from, T& to)
{
    if (from.size() != sizeof(T))
        return client_errc::protocol_value_error;
    to = boost::endian::endian_load<T, sizeof(T), boost::endian::order::big>(from.data());
    return {};
}

}  // namespace

boost::system::error_code detail::field_parse<std::int16_t>::call(
    std::optional<std::span<const unsigned char>> from,
    const protocol::field_description& desc,
    std::int16_t& to
)
{
    if (!from.has_value())
        return client_errc::unexpected_null;
    BOOST_ASSERT(desc.type_oid == int2_oid);
    return desc.fmt_code == protocol::format_code::text ? parse_text_int(*from, to)
                                                        : parse_binary_int(*from, to);
}

boost::system::error_code detail::field_parse<std::int32_t>::call(
    std::optional<std::span<const unsigned char>> from,
    const protocol::field_description& desc,
    std::int32_t& to
)
{
    if (!from.has_value())
        return client_errc::unexpected_null;
    auto data = *from;
    switch (desc.type_oid)
    {
        case int2_oid:
        {
            std::int16_t value{};
            auto ec = desc.fmt_code == protocol::format_code::text ? parse_text_int(data, value)
                                                                   : parse_binary_int(data, value);
            to = value;
            return ec;
        }
        case int4_oid:
            return desc.fmt_code == protocol::format_code::text ? parse_text_int(data, to)
                                                                : parse_binary_int(data, to);
        default: BOOST_ASSERT(false); return {};
    }
}

boost::system::error_code detail::field_parse<std::int64_t>::call(
    std::optional<std::span<const unsigned char>> from,
    const protocol::field_description& desc,
    std::int64_t& to
)
{
    if (!from.has_value())
        return client_errc::unexpected_null;
    auto data = *from;
    switch (desc.type_oid)
    {
        case int2_oid:
        {
            std::int16_t value{};
            auto ec = desc.fmt_code == protocol::format_code::text ? parse_text_int(data, value)
                                                                   : parse_binary_int(data, value);
            to = value;
            return ec;
        }
        case int4_oid:
        {
            std::int32_t value{};
            auto ec = desc.fmt_code == protocol::format_code::text ? parse_text_int(data, value)
                                                                   : parse_binary_int(data, value);
            to = value;
            return ec;
        }
        case int8_oid:
            return desc.fmt_code == protocol::format_code::text ? parse_text_int(data, to)
                                                                : parse_binary_int(data, to);
        default: BOOST_ASSERT(false); return {};
    }
}

boost::system::error_code detail::compute_pos_map(
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

handler_setup_result detail::resultset_setup(const request& req, std::size_t offset)
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
        return {static_cast<std::size_t>(it - msgs.begin())};
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
    return execute_found ? handler_setup_result{static_cast<std::size_t>(it - msgs.begin())}
                         : handler_setup_result{client_errc::incompatible_response_type};
}
