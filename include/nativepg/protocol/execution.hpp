//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_PROTOCOL_EXECUTION_HPP
#define NATIVEPG_PROTOCOL_EXECUTION_HPP

#include <boost/core/span.hpp>

#include <optional>

#include "nativepg/protocol/common.hpp"
#include "nativepg/protocol/views.hpp"

namespace nativepg {
namespace protocol {

// Forward decl
struct field_description;

namespace detail {

// Collections impl
template <>
struct forward_traits<std::optional<boost::span<const unsigned char>>>
{
    static std::optional<boost::span<const unsigned char>> dereference(const unsigned char* data);
    static const unsigned char* advance(const unsigned char* data);
};

template <>
struct forward_traits<field_description>
{
    static field_description dereference(const unsigned char* data);
    static const unsigned char* advance(const unsigned char* data);
};

template <>
struct random_access_traits<std::int32_t>
{
    static std::int32_t dereference(const unsigned char* data);
};

}  // namespace detail

struct close_complete
{
};
inline boost::system::error_code parse(boost::span<const unsigned char> data, close_complete&)
{
    return detail::check_empty(data);
}

struct command_complete
{
    // The command tag. This is usually a single word that identifies which SQL command was completed.
    std::string_view tag;
};
boost::system::error_code parse(boost::span<const unsigned char> data, command_complete& to);

struct data_row
{
    // The actual values. Contains a an optional<span<const unsigned char>> per column,
    // containing the serialized value, or an empty optional, if the field is NULL
    forward_parsing_view<std::optional<boost::span<const unsigned char>>> columns;
};
boost::system::error_code parse(boost::span<const unsigned char> data, data_row& to);

struct empty_query_response
{
};
inline boost::system::error_code parse(boost::span<const unsigned char> data, empty_query_response&)
{
    return detail::check_empty(data);
}

struct no_data
{
};
inline boost::system::error_code parse(boost::span<const unsigned char> data, no_data&)
{
    return detail::check_empty(data);
}

// Describes the parameters of a statement
struct parameter_description
{
    // The type OIDs of the statement parameters, one for each parameter
    random_access_parsing_view<std::int32_t> parameter_type_oids;
};
boost::system::error_code parse(boost::span<const unsigned char> data, parameter_description& to);

struct parse_complete
{
};
inline boost::system::error_code parse(boost::span<const unsigned char> data, parse_complete&)
{
    return detail::check_empty(data);
}

struct portal_suspended
{
};
inline boost::system::error_code parse(boost::span<const unsigned char> data, portal_suspended&)
{
    return detail::check_empty(data);
}

// Sent when operations complete
enum class transaction_status : unsigned char
{
    // not in a transaction block
    idle = 'I',

    // in a transaction block
    in_transaction = 'T',

    // in a failed transaction block (queries will be rejected until block is ended)
    failed = 'E',
};

struct ready_for_query
{
    // Current backend transaction status indicator
    transaction_status status;
};
boost::system::error_code parse(boost::span<const unsigned char> data, ready_for_query& to);

// A description of a single field
struct field_description
{
    // The field name.
    std::string_view name;

    // If the field can be identified as a column of a specific table, the object ID of the table; otherwise
    // zero.
    std::int32_t table_oid;

    // If the field can be identified as a column of a specific table, the attribute number of the column;
    // otherwise zero.
    std::int16_t column_attribute;

    // The object ID of the field's data type.
    std::int32_t type_oid;

    // The data type size (see pg_type.typlen). Note that negative values denote variable-width types.
    std::int16_t type_length;

    // The type modifier (see pg_attribute.atttypmod). The meaning of the modifier is type-specific.
    std::int32_t type_modifier;

    // The format code being used for the field. Currently will be zero (text) or one (binary). In a
    // RowDescription returned from the statement variant of Describe, the format code is not yet known and
    // will always be zero.
    format_code fmt_code;
};

struct row_description
{
    // A collection of descriptions, one per column
    forward_parsing_view<field_description> field_descriptions;
};
boost::system::error_code parse(boost::span<const unsigned char> data, row_description& to);

}  // namespace protocol
}  // namespace nativepg

#endif
