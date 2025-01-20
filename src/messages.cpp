
#include <boost/core/span.hpp>
#include <boost/endian/conversion.hpp>
#include <boost/system/error_code.hpp>

#include <cstddef>
#include <cstdint>

#include "nativepg/client_errc.hpp"
#include "nativepg/wire/messages.hpp"
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

}  // namespace

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
    command_complete& to
)
{
    detail::parse_context ctx(data);
    to.tag = ctx.get_string();
    return ctx.check();
}

// Each field is: Int32 size + Byte<n>
boost::span<const unsigned char> nativepg::protocol::columns_view::iterator::dereference() const
{
    auto size = static_cast<std::size_t>(boost::endian::load_big_s32(data_));
    return {data_ + 4u, size};
}

void nativepg::protocol::columns_view::iterator::advance()
{
    data_ += (static_cast<std::size_t>(boost::endian::load_big_s32(data_)) + 4u);
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
    to.columns = columns_view(num_columns, {values_begin, values_end});

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