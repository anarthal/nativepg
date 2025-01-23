//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/charconv/from_chars.hpp>
#include <boost/charconv/limits.hpp>
#include <boost/core/span.hpp>
#include <boost/endian/conversion.hpp>
#include <boost/system/error_code.hpp>
#include <boost/throw_exception.hpp>

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <system_error>

#include "nativepg/client_errc.hpp"
#include "nativepg/wire/messages.hpp"
#include "nativepg/wire/views.hpp"
#include "parse_context.hpp"

using namespace nativepg::protocol;

namespace {

enum class backend_message_type : char
{
    authentication_request = 'R',
    backend_key_data = 'K',
    bind_complete = '2',
    close_complete = '3',
    command_complete = 'C',
    copy_in_response = 'G',
    copy_out_response = 'H',
    copy_both_response = 'W',
    data_row = 'D',
    empty_query_response = 'I',
    error_response = 'E',
    negotiate_protocol_version = 'v',
    no_data = 'n',
    notice_response = 'N',
    notification_response = 'A',
    parameter_description = 't',
    parameter_status = 'S',
    parse_complete = '1',
    portal_suspended = 's',
    ready_for_query = 'Z',
    row_description = 'T',
    copy_data = 'd',
    copy_done = 'c'
};

enum class authentication_message_type : std::int32_t
{
    ok = 0,
    cleartext_password = 3,
    md5_password = 5,
};

template <class MessageType>
boost::system::result<any_backend_message> parse_impl(boost::span<const unsigned char> from)
{
    MessageType res;
    auto ec = nativepg::protocol::parse(from, res);
    if (ec)
        return ec;
    return res;
}

boost::system::result<any_backend_message> parse_authentication_request(boost::span<const unsigned char> from)
{
    // Get the authentication request type code
    if (from.size() < 4u)
        return nativepg::client_errc::incomplete_message;
    auto type = static_cast<authentication_message_type>(boost::endian::load_big_s32(from.data()));
    from = from.subspan(4);

    // Parse
    switch (type)
    {
    case authentication_message_type::ok: return parse_impl<authentication_ok>(from);
    case authentication_message_type::cleartext_password:
        return parse_impl<authentication_cleartext_password>(from);
    case authentication_message_type::md5_password: return parse_impl<authentication_md5_password>(from);
    default: return nativepg::client_errc::protocol_value_error;
    }
}

// https://www.postgresql.org/docs/current/protocol-error-fields.html
enum class error_field_type : unsigned char
{
    severity_i18n = 'S',
    severity = 'V',
    sqlstate = 'C',
    message = 'M',
    detail = 'D',
    hint = 'H',
    position = 'P',
    internal_position = 'p',
    internal_query = 'q',
    where = 'W',
    schema_name = 's',
    table_name = 't',
    column_name = 'c',
    data_type_name = 'd',
    constraint_name = 'n',
    file_name = 'F',
    line_number = 'L',
    routine = 'R',
};

void populate_field(detail::parse_context& ctx, error_field_type type, error_response& to)
{
    switch (type)
    {
    case error_field_type::severity_i18n: to.localized_severity = ctx.get_string(); break;
    case error_field_type::severity: to.severity = ctx.get_string(); break;
    case error_field_type::sqlstate: to.sqlstate = ctx.get_string(); break;
    case error_field_type::message: to.message = ctx.get_string(); break;
    case error_field_type::detail: to.detail = ctx.get_string(); break;
    case error_field_type::hint: to.hint = ctx.get_string(); break;
    case error_field_type::position: to.position = ctx.get_string(); break;
    case error_field_type::internal_position: to.internal_position = ctx.get_string(); break;
    case error_field_type::internal_query: to.internal_query = ctx.get_string(); break;
    case error_field_type::where: to.where = ctx.get_string(); break;
    case error_field_type::schema_name: to.schema_name = ctx.get_string(); break;
    case error_field_type::table_name: to.table_name = ctx.get_string(); break;
    case error_field_type::column_name: to.column_name = ctx.get_string(); break;
    case error_field_type::data_type_name: to.data_type_name = ctx.get_string(); break;
    case error_field_type::constraint_name: to.constraint_name = ctx.get_string(); break;
    case error_field_type::file_name: to.file_name = ctx.get_string(); break;
    case error_field_type::line_number: to.line_number = ctx.get_string(); break;
    case error_field_type::routine: to.routine = ctx.get_string(); break;
    default: break;  // Intentionally ignore unknown fields
    }
}

bool is_valid_status(transaction_status v)
{
    switch (v)
    {
    case transaction_status::idle:
    case transaction_status::in_transaction:
    case transaction_status::failed: return true;
    default: return false;
    }
}

bool is_valid_format_code(format_code v)
{
    switch (v)
    {
    case format_code::text:
    case format_code::binary: return true;
    default: return false;
    }
}

// The size of the fixed-length fields in a field description of a RowDescription message
constexpr std::size_t field_description_fixed_size = 18u;

// Like format_code, but only in 1 byte. Used in copy messages
enum class overall_format_code : std::uint8_t
{
    text = 0,
    binary = 1,
};

bool is_valid_format_code(overall_format_code v)
{
    switch (v)
    {
    case overall_format_code::text:
    case overall_format_code::binary: return true;
    default: return false;
    }
}

// Shared between copy_in_response, copy_out_response, copy_both_response
boost::system::error_code parse_copy_response(
    boost::span<const unsigned char> data,
    format_code& overall_code_output,
    random_access_parsing_view<format_code>& fmt_codes_output
)
{
    detail::parse_context ctx(data);

    // Overall format code
    auto overall_code = static_cast<overall_format_code>(ctx.get_byte());
    if (!is_valid_format_code(overall_code))
    {
        ctx.add_error(nativepg::client_errc::protocol_value_error);
        return ctx.check();
    }

    // If the overall format is text, subsequent codes should be text, too.
    const bool should_be_text = overall_code == overall_format_code::text;

    // Number of format codes
    auto num_items = static_cast<std::size_t>(ctx.get_nonnegative_integral<std::int16_t>());

    // Individual format codes start here
    const auto* fmt_codes_first = ctx.first();

    // Check each code
    for (std::size_t i = 0; i < num_items; ++i)
    {
        auto code = static_cast<format_code>(ctx.get_integral<std::int16_t>());
        if ((should_be_text && code != format_code::text) || !is_valid_format_code(code))
            ctx.add_error(nativepg::client_errc::protocol_value_error);
    }

    // Populate the response
    overall_code_output = static_cast<format_code>(static_cast<std::int16_t>(overall_code));
    fmt_codes_output = {fmt_codes_first, num_items};

    return ctx.check();
}

}  // namespace

void nativepg::protocol::detail::at_range_check(std::size_t i, std::size_t collection_size)
{
    if (i >= collection_size)
        BOOST_THROW_EXCEPTION(std::out_of_range("random_access_parsing_view::at"));
}

void nativepg::protocol::serialize_header(message_header header, boost::span<unsigned char, 5> dest)
{
    // TODO: this can result in an overflow
    unsigned char* ptr = dest.data();
    *ptr++ = header.type;
    boost::endian::store_big_s32(ptr, header.size);
}

message_header nativepg::protocol::parse_header(boost::span<const unsigned char, 5> from)
{
    // TODO: this can technically be negative!
    return {from[0], boost::endian::load_big_s32(from.data() + 1u)};
}

boost::system::error_code nativepg::protocol::parse(
    boost::span<const unsigned char> data,
    backend_key_data& to
)
{
    detail::parse_context ctx(data);
    to.process_id = ctx.get_integral<std::int32_t>();
    to.secret_key = ctx.get_integral<std::int32_t>();
    return ctx.check();
}

boost::system::error_code nativepg::protocol::parse(
    boost::span<const unsigned char> data,
    authentication_md5_password& to
)
{
    detail::parse_context ctx(data);
    to.salt = ctx.get_byte_array<4>();
    return ctx.check();
}

boost::system::error_code nativepg::protocol::parse(
    boost::span<const unsigned char> data,
    authentication_sasl& to
)
{
    detail::parse_context ctx(data);

    // This is a list of strings, terminated by a NULL byte (that is, an empty string).
    // Strings start here
    const unsigned char* mechanisms_first = ctx.first();

    // On error, get_string returns an empty string, so this is safe
    std::size_t num_items = 0u;
    while (!ctx.get_string().empty())
        ++num_items;

    // Strings end in the NULL byte - that is, one byte before what we are now.
    // The check avoids UB in case of empty messages
    const auto* current = ctx.first();
    const unsigned char* mechanisms_last = current > mechanisms_first ? current - 1 : current;

    // Set the output value
    to.mechanisms = {
        num_items,
        {mechanisms_first, mechanisms_last}
    };

    // Done
    return ctx.check();
}

boost::system::error_code nativepg::protocol::parse(
    boost::span<const unsigned char> data,
    command_complete& to
)
{
    detail::parse_context ctx(data);
    to.tag = ctx.get_string();
    return ctx.check();
}

format_code nativepg::protocol::detail::random_access_traits<format_code>::dereference(
    const unsigned char* data
)
{
    return static_cast<format_code>(unchecked_get_integral<std::int16_t>(data));
}

boost::system::error_code nativepg::protocol::parse(
    boost::span<const unsigned char> data,
    copy_in_response& to
)
{
    return parse_copy_response(data, to.overall_fmt_code, to.fmt_codes);
}

boost::system::error_code nativepg::protocol::parse(
    boost::span<const unsigned char> data,
    copy_out_response& to
)
{
    return parse_copy_response(data, to.overall_fmt_code, to.fmt_codes);
}

boost::system::error_code nativepg::protocol::parse(
    boost::span<const unsigned char> data,
    copy_both_response& to
)
{
    return parse_copy_response(data, to.overall_fmt_code, to.fmt_codes);
}

// Each field is: Int32 size + Byte<n>
boost::span<const unsigned char> nativepg::protocol::detail::forward_traits<
    boost::span<const unsigned char>>::dereference(const unsigned char* data)
{
    auto size = static_cast<std::size_t>(boost::endian::load_big_s32(data));
    return {data + 4u, size};
}

const unsigned char* nativepg::protocol::detail::forward_traits<boost::span<const unsigned char>>::advance(
    const unsigned char* data
)
{
    return data + boost::endian::load_big_s32(data) + 4u;
}

boost::system::error_code nativepg::protocol::parse(boost::span<const unsigned char> data, data_row& to)
{
    detail::parse_context ctx(data);

    // Get the number of columns
    auto num_columns = static_cast<std::size_t>(ctx.get_nonnegative_integral<std::int16_t>());

    // The values start here, record it
    const auto* values_begin = ctx.first();

    // Iterate over all columns to check if there's any error
    for (std::size_t i = 0u; i < num_columns; ++i)
    {
        // Size of the column value
        auto value_size = static_cast<std::size_t>(ctx.get_nonnegative_integral<std::int32_t>());

        // Column value
        ctx.get_bytes(value_size);
    }

    // The values end here
    const auto* values_end = ctx.first();

    // Set the output value
    to.columns = forward_parsing_view<boost::span<const unsigned char>>(
        num_columns,
        {values_begin, values_end}
    );

    // Done
    return ctx.check();
}

std::string_view nativepg::protocol::detail::forward_traits<std::string_view>::dereference(
    const unsigned char* data
)
{
    return detail::unchecked_get_string(data);
}

const unsigned char* nativepg::protocol::detail::forward_traits<std::string_view>::advance(
    const unsigned char* data
)
{
    detail::unchecked_get_string(data);
    return data;
}

boost::system::error_code nativepg::protocol::parse(
    boost::span<const unsigned char> data,
    negotiate_protocol_version& to
)
{
    detail::parse_context ctx(data);

    // Minor version
    to.minor_version = ctx.get_nonnegative_integral<std::int32_t>();

    // Number of options
    auto num_ops = static_cast<std::size_t>(ctx.get_nonnegative_integral<std::int32_t>());

    // This is where strings begin
    const auto* opts_first = ctx.first();

    // Check that all strings are well-formed
    for (std::size_t i = 0u; i < num_ops; ++i)
        ctx.get_string();

    // Strings end here
    const auto* opts_last = ctx.first();

    // Set output
    to.non_recognized_options = {
        num_ops,
        {opts_first, opts_last}
    };

    return ctx.check();
}

boost::system::error_code nativepg::protocol::parse(boost::span<const unsigned char> data, error_response& to)
{
    detail::parse_context ctx(data);

    // A collection of fields, with a 1 byte header stating the field meaning, followed by a string.
    // Terminated by a NULL byte.
    while (true)
    {
        // Get the type byte. On error, this returns 0, which is OK
        unsigned char type_byte = ctx.get_byte();
        if (type_byte == 0u)
            break;
        auto field_type = static_cast<error_field_type>(type_byte);

        // Populate the relevant field
        populate_field(ctx, field_type, to);
    }

    return ctx.check();
}

boost::system::error_code nativepg::protocol::parse(
    boost::span<const unsigned char> data,
    notification_response& to
)
{
    detail::parse_context ctx(data);
    to.process_id = ctx.get_integral<std::int32_t>();
    to.channel_name = ctx.get_string();
    to.payload = ctx.get_string();
    return ctx.check();
}

std::optional<std::size_t> nativepg::protocol::error_response::parsed_line_number() const
{
    // If we received no string field, return nothing
    if (!line_number.has_value())
        return {};

    // Attempt to parse the value, return nothing on error
    std::size_t res = 0u;
    const char* first = line_number->data();
    const char* last = first + line_number->size();
    auto result = boost::charconv::from_chars(first, last, res);
    if (result.ec != std::errc() || result.ptr != last)
        return {};
    return res;
}

std::int32_t nativepg::protocol::detail::random_access_traits<std::int32_t>::dereference(
    const unsigned char* ptr
)
{
    return boost::endian::load_big_s32(ptr);
}

boost::system::error_code nativepg::protocol::parse(
    boost::span<const unsigned char> data,
    parameter_description& to
)
{
    detail::parse_context ctx(data);

    // Get the number of parameters (Int16)
    auto num_params = static_cast<std::size_t>(ctx.get_nonnegative_integral<std::int16_t>());

    // Check that there is space for all the parameters (an Int32 for each parameter).
    // No need to deserialize them, since ints can't fail deserialization.
    // num_params*4 can't overflow in this context AFAIK
    ctx.check_size_and_advance(num_params * 4u);

    // Done
    return ctx.check();
}

boost::system::error_code nativepg::protocol::parse(
    boost::span<const unsigned char> data,
    parameter_status& to
)
{
    detail::parse_context ctx(data);
    to.name = ctx.get_string();
    to.value = ctx.get_string();
    return ctx.check();
}

boost::system::error_code nativepg::protocol::parse(
    boost::span<const unsigned char> data,
    ready_for_query& to
)
{
    detail::parse_context ctx(data);

    // The status is one byte. Get it and check that what we got is one of the valid values.
    // On error, returns a zero byte, which is not valid, either
    auto status = static_cast<transaction_status>(ctx.get_byte());
    if (!is_valid_status(status))
    {
        ctx.add_error(client_errc::protocol_value_error);
    }
    to.status = status;

    return ctx.check();
}

nativepg::protocol::field_description nativepg::protocol::detail::forward_traits<
    field_description>::dereference(const unsigned char* data)
{
    // Evaluation order of initializers is well defined
    return {
        detail::unchecked_get_string(data),                  // name
        detail::unchecked_get_integral<std::int32_t>(data),  // table_oid
        detail::unchecked_get_integral<std::int16_t>(data),  // column_attribute
        detail::unchecked_get_integral<std::int32_t>(data),  // type_oid
        detail::unchecked_get_integral<std::int16_t>(data),  // type_length
        detail::unchecked_get_integral<std::int32_t>(data),  // type_modifier
        static_cast<format_code>(detail::unchecked_get_integral<std::uint8_t>(data)),
    };
}

const unsigned char* nativepg::protocol::detail::forward_traits<field_description>::advance(
    const unsigned char* data
)
{
    // The string is the only variable-size item. Skip it
    detail::unchecked_get_string(data);

    // Skip the fixed fields
    return data + field_description_fixed_size;
}

boost::system::error_code nativepg::protocol::parse(
    boost::span<const unsigned char> data,
    row_description& to
)
{
    detail::parse_context ctx(data);

    // Get the number of items (Int16)
    auto num_items = static_cast<std::size_t>(ctx.get_nonnegative_integral<std::int16_t>());

    // Message to pass to the forward parsing collection starts here
    const auto* data_first = ctx.first();

    // Iterate over all items to check that the format is valid
    for (std::size_t i = 0u; i < num_items; ++i)
    {
        // The name (string) is variable size, so we must get it
        ctx.get_string();

        // We don't need to check the validity of fixed fields, since they are integrals.
        // Stop before the format code field, which needs to be checked
        ctx.check_size_and_advance(field_description_fixed_size - 2u);

        // Check that the format code is known
        auto fmt_code = static_cast<format_code>(ctx.get_integral<std::int16_t>());
        if (!is_valid_format_code(fmt_code))
            ctx.add_error(client_errc::protocol_value_error);
    }

    // Message to pass to the forward parsing collection ends here
    const auto* data_last = ctx.first();
    to.field_descriptions = {
        num_items,
        {data_first, data_last}
    };

    // Done
    return ctx.check();
}

boost::system::result<any_backend_message> nativepg::protocol::parse(
    std::uint8_t message_type,
    boost::span<const unsigned char> data
)
{
    // Cast the message type
    auto t = static_cast<backend_message_type>(message_type);

    switch (t)
    {
    case backend_message_type::authentication_request: return parse_authentication_request(data);
    case backend_message_type::backend_key_data: return parse_impl<backend_key_data>(data);
    // case backend_message_type::bind_complete: return parse_impl<bind_complete_message>(t, data);
    // case backend_message_type::close_complete: return parse_message_impl<close_complete_message>(t, data);
    // case backend_message_type::command_complete: return parse_message_impl<command_complete_message>(t,
    // data);
    default: return nativepg::client_errc::protocol_value_error;
    }
}