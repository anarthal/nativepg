//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_PROTOCOL_DESCRIBE_HPP
#define NATIVEPG_PROTOCOL_DESCRIBE_HPP

#include <boost/core/span.hpp>
#include <boost/system/error_code.hpp>

#include <cstdint>
#include <vector>

#include "nativepg/protocol/common.hpp"
#include "nativepg/protocol/views.hpp"

namespace nativepg {
namespace protocol {

// Forward decl
struct field_description;

namespace detail {

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

// Request to describe a statement or parameter
struct describe
{
    static constexpr unsigned char message_type = static_cast<unsigned char>('D');

    // 'S' to describe a prepared statement; or 'P' to describe a portal.
    portal_or_statement type;

    // The name of the prepared statement or portal to describe (an empty string selects the unnamed prepared
    // statement or portal).
    std::string_view name;
};
boost::system::error_code serialize(const describe& msg, std::vector<unsigned char>& to);

// Describes the parameters of a statement
struct parameter_description
{
    // The type OIDs of the statement parameters, one for each parameter
    random_access_parsing_view<std::int32_t> parameter_type_oids;
};
boost::system::error_code parse(boost::span<const unsigned char> data, parameter_description& to);

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

// Returned if a statement or portal doesn't return data
struct no_data
{
};
inline boost::system::error_code parse(boost::span<const unsigned char> data, no_data&)
{
    return detail::check_empty(data);
}

}  // namespace protocol
}  // namespace nativepg

#endif
