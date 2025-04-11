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
#include <boost/system/result.hpp>
#include <boost/throw_exception.hpp>
#include <boost/variant2/variant.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>
#include <system_error>
#include <vector>

#include "nativepg/client_errc.hpp"
#include "nativepg/protocol/async.hpp"
#include "nativepg/protocol/bind.hpp"
#include "nativepg/protocol/cancel_request.hpp"
#include "nativepg/protocol/close.hpp"
#include "nativepg/protocol/command_complete.hpp"
#include "nativepg/protocol/common.hpp"
#include "nativepg/protocol/copy.hpp"
#include "nativepg/protocol/data_row.hpp"
#include "nativepg/protocol/describe.hpp"
#include "nativepg/protocol/execute.hpp"
#include "nativepg/protocol/flush.hpp"
#include "nativepg/protocol/header.hpp"
#include "nativepg/protocol/notice_error.hpp"
#include "nativepg/protocol/parse.hpp"
#include "nativepg/protocol/query.hpp"
#include "nativepg/protocol/ready_for_query.hpp"
#include "nativepg/protocol/scram_sha256.hpp"
#include "nativepg/protocol/startup.hpp"
#include "nativepg/protocol/sync.hpp"
#include "nativepg/protocol/terminate.hpp"
#include "nativepg_internal/base64.hpp"
#include "parse_context.hpp"
#include "serialization_context.hpp"

using namespace nativepg::protocol;

namespace {

enum class backend_message_type : char
{
    authentication_request = 'R',
    backend_key_data = 'K',
    bind_complete = '2',
    close_complete = '3',
    command_complete = 'C',
    copy_data = 'd',
    copy_done = 'c',
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
};

enum class authentication_message_type : std::int32_t
{
    ok = 0,
    kerberos_v5 = 2,
    cleartext_password = 3,
    md5_password = 5,
    gss = 7,
    gss_continue = 8,
    sspi = 9,
    sasl = 10,
    sasl_continue = 11,
    sasl_final = 12,
};

// template <class MessageType>
// boost::system::result<any_backend_message> parse_impl(boost::span<const unsigned char> from)
// {
//     MessageType res;
//     auto ec = nativepg::protocol::parse(from, res);
//     if (ec)
//         return ec;
//     return res;
// }

// boost::system::result<any_backend_message> parse_authentication_request(boost::span<const unsigned char>
// from)
// {
//     // Get the authentication request type code
//     if (from.size() < 4u)
//         return nativepg::client_errc::incomplete_message;
//     auto type = static_cast<authentication_message_type>(boost::endian::load_big_s32(from.data()));
//     from = from.subspan(4);

//     // Parse
//     switch (type)
//     {
//     case authentication_message_type::ok: return parse_impl<authentication_ok>(from);
//     case authentication_message_type::kerberos_v5: return parse_impl<authentication_kerberos_v5>(from);
//     case authentication_message_type::cleartext_password:
//         return parse_impl<authentication_cleartext_password>(from);
//     case authentication_message_type::md5_password: return parse_impl<authentication_md5_password>(from);
//     case authentication_message_type::gss: return parse_impl<authentication_gss>(from);
//     case authentication_message_type::gss_continue: return parse_impl<authentication_gss_continue>(from);
//     case authentication_message_type::sspi: return parse_impl<authentication_sspi>(from);
//     case authentication_message_type::sasl: return parse_impl<authentication_sasl>(from);
//     case authentication_message_type::sasl_continue: return parse_impl<authentication_sasl_continue>(from);
//     case authentication_message_type::sasl_final: return parse_impl<authentication_sasl_final>(from);
//     default: return nativepg::client_errc::protocol_value_error;
//     }
// }

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

void populate_field(detail::parse_context& ctx, error_field_type type, error_notice_fields& to)
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

// Shared between errors and notices
boost::system::error_code parse_error_notice(boost::span<const unsigned char> data, error_notice_fields& to)
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

// For messages that only have a header
boost::system::error_code serialize_header_only(char header, std::vector<unsigned char>& to)
{
    auto buff = serialize_header({static_cast<unsigned char>(header), 5u});
    BOOST_ASSERT(buff.has_value());
    to.insert(to.end(), buff->begin(), buff->end());
    return {};
}

}  // namespace

void nativepg::protocol::detail::at_range_check(std::size_t i, std::size_t collection_size)
{
    if (i >= collection_size)
        BOOST_THROW_EXCEPTION(std::out_of_range("random_access_parsing_view::at"));
}

boost::system::result<std::array<unsigned char, 5>> nativepg::protocol::serialize_header(message_header header
)
{
    std::array<unsigned char, 5> res{};

    // Range check the length. It should fit an int32, counting the 4 extra bytes in the length field
    constexpr std::size_t max_size = (std::numeric_limits<std::int32_t>::max)() - 4u;
    if (header.size > max_size)
        return client_errc::value_too_big;

    // Message type
    res[0] = header.type;

    // Length
    boost::endian::store_big_s32(res.data() + 1, static_cast<std::int32_t>(header.size));

    // Done
    return res;
}

boost::system::result<message_header> nativepg::protocol::parse_header(
    boost::span<const unsigned char, 5> from
)
{
    // Deserialize individual fields
    unsigned char msg_type = from[0];
    auto size = boost::endian::load_big_s32(from.data() + 1u);

    // Range check the length. The actual length (4 bytes) is included in this count
    if (size < 4)
        return client_errc::protocol_value_error;

    // Done
    return message_header{msg_type, size - 4u};
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
std::optional<boost::span<const unsigned char>> nativepg::protocol::detail::forward_traits<
    std::optional<boost::span<const unsigned char>>>::dereference(const unsigned char* data)
{
    auto size = boost::endian::load_big_s32(data);
    if (size == -1)
        return std::nullopt;
    return boost::span<const unsigned char>(data + 4u, static_cast<std::size_t>(size));
}

const unsigned char* nativepg::protocol::detail::forward_traits<
    std::optional<boost::span<const unsigned char>>>::advance(const unsigned char* data)
{
    auto size = boost::endian::load_big_s32(data);
    return data + 4u + (size > 0 ? size : 0);
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
        auto value_size = ctx.get_integral<std::int32_t>();

        // -1 means NULL
        if (value_size == -1)
            continue;

        // Otherwise, the value should be positive, and indicates the value's length
        if (value_size >= 0)
            ctx.check_size_and_advance(value_size);
        else
            ctx.add_error(client_errc::protocol_value_error);
    }

    // The values end here
    const auto* values_end = ctx.first();

    // Set the output value
    to.columns = forward_parsing_view<std::optional<boost::span<const unsigned char>>>(
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

std::optional<std::size_t> nativepg::protocol::error_notice_fields::parsed_line_number() const
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

boost::system::error_code nativepg::protocol::parse(boost::span<const unsigned char> data, error_response& to)
{
    return parse_error_notice(data, to);
}

boost::system::error_code nativepg::protocol::parse(
    boost::span<const unsigned char> data,
    notice_response& to
)
{
    return parse_error_notice(data, to);
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

// boost::system::result<any_backend_message> nativepg::protocol::parse(
//     std::uint8_t message_type,
//     boost::span<const unsigned char> data
// )
// {
//     // Cast the message type
//     auto t = static_cast<backend_message_type>(message_type);

//     switch (t)
//     {
//     case backend_message_type::authentication_request: return parse_authentication_request(data);
//     case backend_message_type::backend_key_data: return parse_impl<backend_key_data>(data);
//     case backend_message_type::bind_complete: return parse_impl<bind_complete>(data);
//     case backend_message_type::close_complete: return parse_impl<close_complete>(data);
//     case backend_message_type::command_complete: return parse_impl<command_complete>(data);
//     case backend_message_type::copy_data: return parse_impl<copy_data>(data);
//     case backend_message_type::copy_done: return parse_impl<copy_done>(data);
//     case backend_message_type::copy_in_response: return parse_impl<copy_in_response>(data);
//     case backend_message_type::copy_out_response: return parse_impl<copy_out_response>(data);
//     case backend_message_type::copy_both_response: return parse_impl<copy_both_response>(data);
//     case backend_message_type::data_row: return parse_impl<data_row>(data);
//     case backend_message_type::empty_query_response: return parse_impl<empty_query_response>(data);
//     case backend_message_type::error_response: return parse_impl<error_response>(data);
//     case backend_message_type::negotiate_protocol_version:
//         return parse_impl<negotiate_protocol_version>(data);
//     case backend_message_type::no_data: return parse_impl<no_data>(data);
//     case backend_message_type::notice_response: return parse_impl<notice_response>(data);
//     case backend_message_type::notification_response: return parse_impl<notification_response>(data);
//     case backend_message_type::parameter_description: return parse_impl<parameter_description>(data);
//     case backend_message_type::parameter_status: return parse_impl<parameter_status>(data);
//     case backend_message_type::parse_complete: return parse_impl<parse_complete>(data);
//     case backend_message_type::portal_suspended: return parse_impl<portal_suspended>(data);
//     case backend_message_type::ready_for_query: return parse_impl<ready_for_query>(data);
//     case backend_message_type::row_description: return parse_impl<row_description>(data);
//     default: return nativepg::client_errc::protocol_value_error;
//     }
// }

struct nativepg::protocol::detail::bind_context_access
{
    static std::size_t num_params(const bind_context& ctx) { return ctx.num_params_; }
    static void maybe_finish_parameter(bind_context& ctx) { ctx.maybe_finish_parameter(); }
};

namespace {

struct format_codes_serializer
{
    detail::serialization_context& ctx;

    void operator()(format_code f) const { ctx.add_integral(static_cast<std::int16_t>(f)); }
    void operator()(boost::span<const format_code> codes) const
    {
        // Size check
        if (codes.size() > (std::numeric_limits<std::int16_t>::max)())
        {
            ctx.add_error(nativepg::client_errc::value_too_big);
        }
        else
        {
            ctx.add_integral(static_cast<std::int16_t>(codes.size()));
            for (auto c : codes)
                ctx.add_integral(static_cast<std::int16_t>(c));
        }
    }
};

void serialize_fmt_codes(const bind::format_codes& fmt_codes, detail::serialization_context& ctx)
{
    boost::variant2::visit(format_codes_serializer{ctx}, fmt_codes);
}

void serialize_params(
    boost::compat::function_ref<void(bind_context&)> parameters_fn,
    detail::serialization_context& ctx
)
{
    // Allocate space for the number of parameters (not yet known)
    auto& buffer = ctx.buffer();
    std::size_t num_params_offset = buffer.size();
    buffer.resize(buffer.size() + 2u);

    // Call the user function, which will serialize all the parameters
    bind_context bind_ctx(buffer);
    parameters_fn(bind_ctx);
    detail::bind_context_access::maybe_finish_parameter(bind_ctx);

    // Check for errors
    ctx.add_error(bind_ctx.error());

    // Serialize the number of parameters
    std::size_t num_params = detail::bind_context_access::num_params(bind_ctx);
    if (num_params > (std::numeric_limits<std::int16_t>::max)())
    {
        ctx.add_error(nativepg::client_errc::value_too_big);
        return;
    }
    boost::endian::store_big_s16(buffer.data() + num_params_offset, static_cast<std::int16_t>(num_params));
}

}  // namespace

void nativepg::protocol::bind_context::maybe_finish_parameter()
{
    // If there is no pending parameter, do nothing
    if (param_offset_ == no_offset)
        return;

    // Compute the size of the parameter, as the number of bytes added by the user minus the header
    BOOST_ASSERT(buff_.size() > param_offset_ + 4u);
    std::size_t param_size = buff_.size() - param_offset_ - 4u;

    // If the size exceeds INT32_MAX, error
    if (param_size > (std::numeric_limits<std::int32_t>::max)())
    {
        add_error(client_errc::value_too_big);
        return;
    }

    // Write the parameter size
    boost::endian::store_big_s32(buff_.data() + param_offset_, static_cast<std::int32_t>(param_size));

    // Clean up the offset
    param_offset_ = no_offset;
}

boost::system::error_code nativepg::protocol::serialize(const bind& msg, std::vector<unsigned char>& to)
{
    detail::serialization_context ctx(to);

    // Header
    ctx.add_header('B');

    // Portal and statement name
    ctx.add_string(msg.portal_name);
    ctx.add_string(msg.statement_name);

    // Parameter format codes
    serialize_fmt_codes(msg.parameter_fmt_codes, ctx);

    // Serialize the parameters
    serialize_params(msg.parameters_fn, ctx);

    // Result format codes
    serialize_fmt_codes(msg.result_fmt_codes, ctx);

    return ctx.finalize_message();
}

boost::system::error_code nativepg::protocol::serialize(const describe& msg, std::vector<unsigned char>& to)
{
    detail::serialization_context ctx(to);

    // Header
    ctx.add_header('D');

    // Contents
    ctx.add_byte(static_cast<unsigned char>(msg.type));
    ctx.add_string(msg.name);

    // Done
    return ctx.finalize_message();
}

boost::system::error_code nativepg::protocol::serialize(const close& msg, std::vector<unsigned char>& to)
{
    detail::serialization_context ctx(to);

    // Header
    ctx.add_header('C');

    // Contents
    ctx.add_byte(static_cast<unsigned char>(msg.type));
    ctx.add_string(msg.name);

    // Done
    return ctx.finalize_message();
}

boost::system::error_code nativepg::protocol::serialize(const parse_t& msg, std::vector<unsigned char>& to)
{
    detail::serialization_context ctx(to);

    // Header
    ctx.add_header('P');

    // Fixed fields
    ctx.add_string(msg.statement_name);
    ctx.add_string(msg.query);

    // Parameter types
    if (msg.parameter_type_oids.size() > (std::numeric_limits<std::int16_t>::max)())
    {
        ctx.add_error(client_errc::value_too_big);
        return ctx.error();
    }
    ctx.add_integral(static_cast<std::int16_t>(msg.parameter_type_oids.size()));
    for (auto code : msg.parameter_type_oids)
        ctx.add_integral(code);

    // Done
    return ctx.finalize_message();
}

boost::system::error_code nativepg::protocol::serialize(const copy_fail& msg, std::vector<unsigned char>& to)
{
    detail::serialization_context ctx(to);

    // Header
    ctx.add_header('f');

    // Fields
    ctx.add_string(msg.error_message);

    // Done
    return ctx.finalize_message();
}

boost::system::error_code nativepg::protocol::serialize(copy_done, std::vector<unsigned char>& to)
{
    detail::serialization_context ctx(to);
    ctx.add_header('c');
    return ctx.finalize_message();
}

boost::system::error_code nativepg::protocol::serialize(const execute& msg, std::vector<unsigned char>& to)
{
    detail::serialization_context ctx(to);

    // Header
    ctx.add_header('E');

    // Fields
    ctx.add_string(msg.portal_name);
    ctx.add_integral(msg.max_num_rows);

    // Done
    return ctx.finalize_message();
}

boost::system::error_code nativepg::protocol::serialize(flush, std::vector<unsigned char>& to)
{
    return serialize_header_only('H', to);
}

boost::system::error_code nativepg::protocol::serialize(sync, std::vector<unsigned char>& to)
{
    return serialize_header_only('S', to);
}

boost::system::error_code nativepg::protocol::serialize(terminate, std::vector<unsigned char>& to)
{
    return serialize_header_only('X', to);
}

boost::system::error_code nativepg::protocol::serialize(const password& msg, std::vector<unsigned char>& to)
{
    detail::serialization_context ctx(to);
    ctx.add_header('p');
    ctx.add_string(msg.password);
    return ctx.finalize_message();
}

boost::system::error_code nativepg::protocol::serialize(
    const startup_message& msg,
    std::vector<unsigned char>& to
)
{
    detail::serialization_context ctx(to);

    // This message does not have a message type, but it does have a length.
    // Allocate space for the length
    auto length_offset = to.size();
    ctx.add_bytes(std::array<unsigned char, 4>{});

    // The protocol version number. The most significant 16 bits are the major version number (3 for the
    // protocol described here). The least significant 16 bits are the minor version number (0 for the
    // protocol described here).
    ctx.add_integral(std::int32_t(196608));

    // Username
    ctx.add_string("user");
    ctx.add_string(msg.user);

    // Database, if present
    if (msg.database.has_value())
    {
        ctx.add_string("database");
        ctx.add_string(*msg.database);
    }

    // Rest of params
    for (auto param : msg.params)
    {
        ctx.add_string(param.first);
        ctx.add_string(param.second);
    }

    // Terminator
    ctx.add_byte(0);

    // Calculate the message length
    BOOST_ASSERT(to.size() >= length_offset + 4);
    auto msg_length = to.size() - length_offset;

    // Range check
    if (msg_length > (std::numeric_limits<std::int32_t>::max)())
        ctx.add_error(client_errc::value_too_big);

    // Check for errors
    auto err = ctx.error();
    if (err)
        return err;

    // If everything was OK, set the message length
    boost::endian::store_big_s32(to.data() + length_offset, static_cast<std::int32_t>(msg_length));

    // Done
    return {};
}

boost::system::error_code nativepg::protocol::serialize(
    const cancel_request& msg,
    std::vector<unsigned char>& to
)
{
    detail::serialization_context ctx(to);

    // The message has no type code. It has a length, but it's constant
    ctx.add_integral(static_cast<std::int32_t>(16));        // length
    ctx.add_integral(static_cast<std::int32_t>(80877102));  // cancel request code
    ctx.add_integral(msg.process_id);
    ctx.add_integral(msg.secret_key);

    return ctx.error();
}

boost::system::error_code nativepg::protocol::serialize(ssl_request, std::vector<unsigned char>& to)
{
    detail::serialization_context ctx(to);

    // The message has no type code and no length
    ctx.add_integral(std::int32_t(80877103));

    return ctx.error();
}

boost::system::error_code nativepg::protocol::serialize(query msg, std::vector<unsigned char>& to)
{
    detail::serialization_context ctx(to);

    // Header
    ctx.add_header('Q');

    // Query
    ctx.add_string(msg.query);

    // Done
    return ctx.error();
}

// scram sha256.
// Message formats described in https://datatracker.ietf.org/doc/html/rfc5802

boost::system::result<boost::span<const unsigned char>> nativepg::protocol::serialize(
    const scram_sha256_client_first_message& msg,
    std::vector<unsigned char>& to
)
{
    detail::serialization_context ctx(to);

    // Header
    ctx.add_header('p');

    // SASL mechanism name
    ctx.add_string(msg.mechanism);

    // The rest of the message is a SCRAM-SHA256 client-first-message.
    // It's preceeded by a 4 byte integer indicating its length.
    // Reserve empty space for this size
    std::size_t length_offset = to.size();
    ctx.add_bytes(std::array<unsigned char, 4>{});

    // client-first-message = gs2-header client-first-message-bare
    // gs2-header      = gs2-cbind-flag "," [ authzid ] ","
    // client-first-message-bare =
    //      [reserved-mext ","] username "," nonce ["," extensions]
    // username        = "n=" saslname ;; always empty in our case, sent in the startup msg
    // nonce           = "r=" c-nonce [s-nonce] ;; printable
    // printable       = %x21-2B / %x2D-7E

    // Add the gs2-header (always "n,," because we don't support channel binding yet)
    ctx.add_bytes("n,,");

    // client-first-message-bare starts here
    std::size_t bare_start_offset = to.size();

    // Add the username (always empty) and the nonce
    // TODO: should we check that the nonce complies with the grammar?
    ctx.add_bytes("n=,r=");
    ctx.add_bytes(msg.nonce);

    // client-first-message-bare ends here
    std::size_t bare_end_offset = to.size();

    // Calculate the length of what we serialized
    auto data_length = to.size() - length_offset - 4u;
    if (data_length > (std::numeric_limits<std::int32_t>::max)())
    {
        ctx.add_error(client_errc::value_too_big);
    }
    else
    {
        boost::endian::store_big_s32(to.data() + length_offset, static_cast<std::int32_t>(data_length));
    }

    // Finalize
    if (auto ec = ctx.finalize_message())
        return ec;

    // Done
    return boost::span<const unsigned char>(to.data() + bare_start_offset, to.data() + bare_end_offset);
}

static bool scram_is_printable(unsigned char c)
{
    return (c >= 0x21 && c <= 0x2b) || (c >= 0x2d && c <= 0x7e);
}

boost::system::error_code nativepg::protocol::parse(
    boost::span<const unsigned char> data,
    scram_sha256_server_first_message& to
)
{
    // server-first-message = [reserved-mext ","] nonce "," salt "," iteration-count ["," extensions]
    // reserved-mext  = "m=" 1*(value-char) ;; if this is present, we're missing extensions and should fail
    // parsing nonce          = "r=" c-nonce [s-nonce] ;; these are equal to printable
    // printable       =%x21-2B / %x2D-7E
    // salt            = "s=" base64
    // iteration-count = "i=" posit-number
    // extensions = attr-val *("," attr-val) ;; to be ignored
    // attr-val        = ALPHA "=" value
    // value           = 1*value-char

    const unsigned char* p = data.data();
    const unsigned char* last = data.data() + data.size();

    // Try to match reserved-mext
    if (p != last && *p == static_cast<unsigned char>('m'))
    {
        ++p;
        return (p == last || *p != ',') ? client_errc::invalid_scram_message
                                        : client_errc::mandatory_scram_extension_not_supported;
    }

    // Parse the nonce
    if (p == last || *p++ != 'r')
        return client_errc::invalid_scram_message;
    if (p == last || *p++ != '=')
        return client_errc::invalid_scram_message;
    const auto* nonce_first = p;
    while (true)
    {
        if (p == last)
            return client_errc::invalid_scram_message;
        if (*p == ',')
            break;
        if (!scram_is_printable(*p))
            return client_errc::invalid_scram_message;
        ++p;
    }
    to.nonce = {reinterpret_cast<const char*>(nonce_first), reinterpret_cast<const char*>(p)};
    ++p;  // skip the final comma

    // Parse the salt
    if (p == last || *p++ != 's')
        return client_errc::invalid_scram_message;
    if (p == last || *p++ != '=')
        return client_errc::invalid_scram_message;
    const auto* salt_first = p;
    while (true)
    {
        if (p == last)
            return client_errc::invalid_scram_message;
        if (*p == ',')
            break;
        ++p;
    }
    auto ec = detail::base64_decode({salt_first, p}, to.salt);
    if (ec)
        return ec;
    ++p;  // skip the final comma

    // Parse the iteration count. Verify that all the characters
    // are numbers, to avoid any possible signed char
    if (p == last || *p++ != 'i')
        return client_errc::invalid_scram_message;
    if (p == last || *p++ != '=')
        return client_errc::invalid_scram_message;
    const char* i_first = reinterpret_cast<const char*>(p);
    const char* i_last = std::find(i_first, reinterpret_cast<const char*>(last), ',');
    auto parse_result = std::from_chars(i_first, i_last, to.iteration_count);
    if (parse_result.ec != std::errc() || parse_result.ptr != i_last)
        return client_errc::invalid_scram_message;

    // TODO: verify that if we got any extensions, they are well-formed
    return {};
}

boost::system::result<boost::span<const unsigned char>> nativepg::protocol::serialize(
    const scram_sha256_client_final_message& msg,
    std::vector<unsigned char>& to
)
{
    detail::serialization_context ctx(to);

    // Header
    ctx.add_header('p');

    // client-final-message = client-final-message-without-proof "," proof
    // client-final-message-without-proof = channel-binding "," nonce ["," extensions]
    // channel-binding = "c=" base64 ;; base64 encoding of cbind-input.
    // cbind-input   = gs2-header [ cbind-data ] ;; cbind-data absent if no channel binding is present
    // gs2-header      = gs2-cbind-flag "," [ authzid ] ","
    // gs2-cbind-flag  = ("p=" cb-name) / "n" / "y" ;; always "n" in our case
    // nonce           = "r=" c-nonce [s-nonce]
    // extensions = attr-val *("," attr-val) ;; none supported right now
    // proof           = "p=" base64

    // client-final-message-without-proof starts here
    std::size_t offset_first = to.size();

    // gs2-header is always "n,," when no channel binding is present
    // this makes channel-binding always equals to "c=biws"
    // nonce ("r=") comes next
    ctx.add_bytes("c=biws,r=");
    ctx.add_bytes(msg.nonce);

    // client-final-message-without-proof ends here
    std::size_t offset_last = to.size();

    // proof
    ctx.add_bytes(",p=");
    detail::base64_encode(msg.proof, to);

    // Finalize message
    if (auto ec = ctx.finalize_message())
        return ec;

    // Done
    return boost::span<const unsigned char>(to.data() + offset_first, to.data() + offset_last);
}
