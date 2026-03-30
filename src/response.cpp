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
#include <chrono>
#include <iostream>
#include <locale>
#include <sstream>
#include <cstring>

#include "nativepg/client_errc.hpp"
#include "nativepg/detail/field_traits.hpp"
#include "nativepg/types.hpp"
#include "nativepg/response.hpp"

using namespace nativepg;
using namespace nativepg::types;
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

boost::system::error_code nativepg::detail::field_parse<std::int16_t>::call(
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

boost::system::error_code nativepg::detail::field_parse<std::int32_t>::call(
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

boost::system::error_code nativepg::detail::field_parse<std::int64_t>::call(
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


// DATE => std::chrono::sys_days
boost::system::error_code nativepg::detail::field_parse<std::chrono::sys_days>::call(
    std::optional<std::span<const unsigned char>> from,
    const protocol::field_description& desc,
    std::chrono::sys_days& to
)
{
    if (!from.has_value())
        return client_errc::unexpected_null;
    BOOST_ASSERT(desc.type_oid == 1082);
    return desc.fmt_code == protocol::format_code::text ?
        parse_text_date(*from, to) :
        parse_binary_date(*from, to);
}

// TIME => std::chrono::microseconds
boost::system::error_code nativepg::detail::field_parse<std::chrono::microseconds>::call(
    std::optional<std::span<const unsigned char>> from,
    const protocol::field_description& desc,
    std::chrono::microseconds& to
)
{
    if (!from.has_value())
        return client_errc::unexpected_null;
    BOOST_ASSERT(desc.type_oid == 1083);
    return desc.fmt_code == protocol::format_code::text ?
        parse_text_time(*from, to) :
        parse_binary_time(*from, to);
}

// TIMETZ => pg_timetz
boost::system::error_code nativepg::detail::field_parse<types::pg_timetz>::call(
    std::optional<std::span<const unsigned char>> from,
    const protocol::field_description& desc,
    types::pg_timetz& to
)
{
    if (!from.has_value())
        return client_errc::unexpected_null;
    BOOST_ASSERT(desc.type_oid == 1266);
    return desc.fmt_code == protocol::format_code::text ?
        parse_text_timetz(*from, to) :
        parse_binary_timetz(*from, to);
}

// TIMESTAMP => pg_timestamp
boost::system::error_code nativepg::detail::field_parse<types::pg_timestamp>::call(
    std::optional<std::span<const unsigned char>> from,
    const protocol::field_description& desc,
    types::pg_timestamp& to
)
{
    if (!from.has_value())
        return client_errc::unexpected_null;
    BOOST_ASSERT(desc.type_oid == 1114);
    return desc.fmt_code == protocol::format_code::text ?
        parse_text_timestamp(*from, to) :
        parse_binary_timestamp(*from, to);
}

// TIMESTAMPTZ => pg_timestamptz
boost::system::error_code nativepg::detail::field_parse<types::pg_timestamptz>::call(
    std::optional<std::span<const unsigned char>> from,
    const protocol::field_description& desc,
    types::pg_timestamptz& to
)
{
    if (!from.has_value())
        return client_errc::unexpected_null;
    BOOST_ASSERT(desc.type_oid == 1184);
    return desc.fmt_code == protocol::format_code::text ?
        parse_text_timestamptz(*from, to) :
        parse_binary_timestamptz(*from, to);
}

// INTERVAL => pg_interval
boost::system::error_code nativepg::detail::field_parse<types::pg_interval>::call(
    std::optional<std::span<const unsigned char>> from,
    const protocol::field_description& desc,
    types::pg_interval& to
)
{
    if (!from.has_value())
        return client_errc::unexpected_null;
    BOOST_ASSERT(desc.type_oid == 1186);
    return desc.fmt_code == protocol::format_code::text ?
        parse_text_interval(*from, to) :
        parse_binary_interval(*from, to);
}

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
