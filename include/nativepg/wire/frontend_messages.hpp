#pragma once

#include <boost/core/span.hpp>

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>

#include "nativepg/wire/serialization_context.hpp"

namespace nativepg {
namespace detail {

enum class format_code : std::int16_t
{
    text = 0,
    binary = 1
};

class any_params_ref
{
    // TODO
public:
    std::size_t size() const;
    void serialize(serialization_context&) const;
};

struct bind_message
{
    static constexpr unsigned char message_type = static_cast<unsigned char>('B');

    // The name of the destination portal (an empty string selects the unnamed portal).
    std::string_view portal_name;

    // The name of the source prepared statement (an empty string selects the unnamed prepared statement).
    std::string_view statement_name;

    // TODO: allow all text or all binary optimization
    boost::span<const format_code> params_format_codes;

    any_params_ref params;

    // TODO: allow all text or all binary optimization
    boost::span<const format_code> result_format_codes;

    void serialize(serialization_context& ctx)
    {
        ctx.add_string(portal_name);
        ctx.add_string(statement_name);
        ctx.add_integral(static_cast<std::int16_t>(params_format_codes.size()));  // size check?
        for (auto code : params_format_codes)
            ctx.add_integral(static_cast<std::int16_t>(code));
        ctx.add_integral(static_cast<std::int16_t>(params.size()));
        ctx.add_integral(static_cast<std::int16_t>(result_format_codes.size()));  // size check?
        for (auto code : result_format_codes)
            ctx.add_integral(static_cast<std::int16_t>(code));
    }
};

enum class portal_or_statement : char
{
    statement = 'S',
    portal = 'P',
};

struct close_message
{
    static constexpr unsigned char message_type = static_cast<unsigned char>('C');

    // 'S' to close a prepared statement; or 'P' to close a portal.
    portal_or_statement type;

    // The name of the prepared statement or portal to close (an empty string selects the unnamed prepared
    // statement or portal).
    std::string_view name;

    void serialize(serialization_context& ctx)
    {
        ctx.add_byte(static_cast<unsigned char>(type));
        ctx.add_string(name);
    }
};

struct copy_fail_message
{
    static constexpr unsigned char message_type = static_cast<unsigned char>('f');

    // An error message to report as the cause of failure.
    std::string_view error_message;

    void serialize(serialization_context& ctx) const { ctx.add_string(error_message); }
};

struct describe_message
{
    static constexpr unsigned char message_type = static_cast<unsigned char>('D');

    // 'S' to describe a prepared statement; or 'P' to describe a portal.
    portal_or_statement type;

    // The name of the prepared statement or portal to describe (an empty string selects the unnamed prepared
    // statement or portal).
    std::string_view name;

    void serialize(serialization_context& ctx) const
    {
        ctx.add_byte(static_cast<unsigned char>(type));
        ctx.add_string(name);
    }
};

struct execute_message
{
    static constexpr unsigned char message_type = static_cast<unsigned char>('E');

    // The name of the portal to execute (an empty string selects the unnamed portal).
    std::string_view portal_name;

    // Maximum number of rows to return, if portal contains a query that returns rows (ignored otherwise).
    // Zero denotes “no limit”.
    std::int32_t max_num_rows;

    void serialize(serialization_context& ctx) const
    {
        ctx.add_string(portal_name);
        ctx.add_integral(max_num_rows);
    }
};

struct flush_message
{
    static constexpr unsigned char message_type = static_cast<unsigned char>('H');

    void serialize(serialization_context&) const {}
};

struct parse_message
{
    static constexpr unsigned char message_type = static_cast<unsigned char>('P');

    // The name of the destination prepared statement (an empty string selects the unnamed prepared
    // statement).
    std::string_view statement_name;

    // The query string to be parsed.
    std::string_view query;

    // Specifies the object ID of the parameter data type. Placing a zero here is equivalent to leaving
    //         the type unspecified.
    boost::span<const std::int32_t> param_types;

    void serialize(serialization_context& ctx) const
    {
        ctx.add_string(statement_name);
        ctx.add_string(query);
        ctx.add_integral(static_cast<std::int16_t>(param_types.size()));
        for (auto code : param_types)
            ctx.add_integral(code);
    }
};

struct password_message
{
    static constexpr unsigned char message_type = static_cast<unsigned char>('p');

    // The password (encrypted, if requested). TODO: does this really have NULL terminator??
    std::string_view password;

    void serialize(serialization_context& ctx) const { ctx.add_string(password); }
};

struct query_message
{
    static constexpr unsigned char message_type = static_cast<unsigned char>('Q');

    // The query string itself.
    std::string_view query;

    void serialize(serialization_context& ctx) const { ctx.add_string(query); }
};

struct sync_message
{
    static constexpr unsigned char message_type = static_cast<unsigned char>('S');

    void serialize(serialization_context& ctx) const {}
};

struct terminate_message
{
    static constexpr unsigned char message_type = static_cast<unsigned char>('X');

    void serialize(serialization_context& ctx) const {}
};

// Note: the below messages don't have a type code
struct ssl_request
{
    void serialize(serialization_context& ctx) const
    {
        // The SSL request code. The value is chosen to contain 1234 in the most significant 16 bits, and 5679
        // in the least significant 16 bits. (To avoid confusion, this code must not be the same as any
        // protocol version number.)
        ctx.add_integral(std::int32_t(80877103));
    }
};

struct startup_message
{
    // The database user name to connect as. Required; there is no default.
    std::string_view user;

    // The database to connect to. Defaults to the user name. Leave empty for no database
    std::string_view database;

    // Additional key/value settings
    boost::span<const std::pair<std::string_view, std::string_view>> params;

    void serialize(serialization_context& ctx) const
    {
        // The protocol version number. The most significant 16 bits are the major version number (3 for the
        // protocol described here). The least significant 16 bits are the minor version number (0 for the
        // protocol described here).
        ctx.add_integral(std::int32_t(196608));

        // Username
        ctx.add_string("user");
        ctx.add_string(user);

        // Database, if present
        if (!database.empty())
        {
            ctx.add_string("database");
            ctx.add_string(database);
        }

        // Rest of params
        for (auto param : params)
        {
            ctx.add_string(param.first);
            ctx.add_string(param.second);
        }

        // Terminator
        ctx.add_byte(0);
    }
};

// Note: this message does not have a type code
struct cancel_request
{
    // The process ID of the target backend.
    std::int32_t process_id;

    // The secret key for the target backend.
    std::int32_t secret_key;

    void serialize(serialization_context& ctx)
    {
        ctx.add_integral(static_cast<std::int32_t>(16));        // length
        ctx.add_integral(static_cast<std::int32_t>(80877102));  // cancel request code
        ctx.add_integral(process_id);
        ctx.add_integral(secret_key);
    }
};

}  // namespace detail
}  // namespace nativepg