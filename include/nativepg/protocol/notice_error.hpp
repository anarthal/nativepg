//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_PROTOCOL_NOTICE_ERROR_HPP
#define NATIVEPG_PROTOCOL_NOTICE_ERROR_HPP

#include <boost/core/span.hpp>
#include <boost/system/error_code.hpp>

#include <optional>
#include <string_view>

namespace nativepg {
namespace protocol {

// Errors and notices. error_response and notice_response share the same structure
// TODO: should we make these const char*?
// All of the fields are optional, and user-defined functions may use them as they like,
// so we tolerate almost anything in them
struct error_notice_fields
{
    // Severity: the field contents are ERROR, FATAL, or PANIC (in an error message), or WARNING, NOTICE,
    // DEBUG, INFO, or LOG (in a notice message). Non-localized.
    std::optional<std::string_view> severity;

    // Severity: the field contents are ERROR, FATAL, or PANIC (in an error message), or WARNING, NOTICE,
    // DEBUG, INFO, or LOG (in a notice message), or a localized translation of one of these.
    std::optional<std::string_view> localized_severity;

    // SQLSTATE code
    std::optional<std::string_view> sqlstate;

    // Message: the primary human-readable error message. This should be accurate but terse (typically one
    // line).
    std::optional<std::string_view> message;

    // Detail: an optional secondary error message carrying more detail about the problem. Might run to
    // multiple lines.
    std::optional<std::string_view> detail;

    // Hint: an optional suggestion what to do about the problem. This is intended to differ from Detail in
    // that it offers advice (potentially inappropriate) rather than hard facts. Might run to multiple lines.
    std::optional<std::string_view> hint;

    // Position: the field value is a decimal ASCII integer, indicating an error cursor position as an index
    // into the original query string. The first character has index 1, and positions are measured in
    // characters not bytes.
    std::optional<std::string_view> position;

    // Internal position: this is defined the same as the P field, but it is used when the cursor position
    // refers to an internally generated command rather than the one submitted by the client. The q field will
    // always appear when this field appears.
    std::optional<std::string_view> internal_position;

    // Internal query: the text of a failed internally-generated command. This could be, for example, an SQL
    // query issued by a PL/pgSQL function.
    std::optional<std::string_view> internal_query;

    // Where: an indication of the context in which the error occurred. Presently this includes a call stack
    // traceback of active procedural language functions and internally-generated queries. The trace is one
    // entry per line, most recent first.
    std::optional<std::string_view> where;

    // Schema name: if the error was associated with a specific database object, the name of the schema
    // containing that object, if any.
    std::optional<std::string_view> schema_name;

    // Table name: if the error was associated with a specific table, the name of the table. (Refer to the
    // schema name field for the name of the table's schema.)
    std::optional<std::string_view> table_name;

    // Column name: if the error was associated with a specific table column, the name of the column. (Refer
    // to the schema and table name fields to identify the table.)
    std::optional<std::string_view> column_name;

    // Data type name: if the error was associated with a specific data type, the name of the data type.
    // (Refer to the schema name field for the name of the data type's schema.)
    std::optional<std::string_view> data_type_name;

    // Constraint name: if the error was associated with a specific constraint, the name of the constraint.
    // Refer to fields listed above for the associated table or domain. (For this purpose, indexes are treated
    // as constraints, even if they weren't created with constraint syntax.)
    std::optional<std::string_view> constraint_name;

    // File: the file name of the source-code location where the error was reported.
    std::optional<std::string_view> file_name;

    // Line: the line number of the source-code location where the error was reported.
    std::optional<std::string_view> line_number;
    std::optional<std::size_t> parsed_line_number() const;

    // Routine: the name of the source-code routine reporting the error.
    std::optional<std::string_view> routine;
};

struct error_response : error_notice_fields
{
};
boost::system::error_code parse(boost::span<const unsigned char> data, error_response& to);

struct notice_response : error_notice_fields
{
};
boost::system::error_code parse(boost::span<const unsigned char> data, notice_response& to);

}  // namespace protocol
}  // namespace nativepg

#endif
