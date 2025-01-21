//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_WIRE_MESSAGES_HPP
#define NATIVEPG_WIRE_MESSAGES_HPP

#include <boost/core/span.hpp>
#include <boost/system/error_code.hpp>
#include <boost/system/result.hpp>
#include <boost/variant2/variant.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <string_view>

#include "nativepg/wire/views.hpp"

namespace nativepg {
namespace protocol {

// Forward decl
struct field_description;

namespace detail {
boost::system::error_code check_empty(boost::span<const unsigned char> data);

// Collections impl
template <>
struct forward_traits<boost::span<const unsigned char>>
{
    static boost::span<const unsigned char> dereference(const unsigned char* data);
    static const unsigned char* advance(const unsigned char* data);
};

template <>
struct forward_traits<field_description>
{
    static field_description dereference(const unsigned char* data);
    static const unsigned char* advance(const unsigned char* data);
};

}  // namespace detail

// Common definitions
enum class format_code : std::int16_t
{
    text = 0,
    binary = 1,
};

// Message header operations
struct message_header
{
    std::uint8_t type;  // The message type
    std::int32_t size;  // Should be >= 0
};
void serialize_header(message_header header, boost::span<unsigned char, 5> dest);
message_header parse_header(boost::span<const unsigned char, 5> from);

//
// Messages that we may receive from the backend
//

// Authentication messages require parsing the following 4 bytes, too
struct authentication_ok
{
};
inline boost::system::error_code parse(boost::span<const unsigned char> data, authentication_ok&)
{
    return detail::check_empty(data);
}

struct authentication_cleartext_password
{
};
inline boost::system::error_code parse(boost::span<const unsigned char> data, authentication_cleartext_password&)
{
    return detail::check_empty(data);
}

struct authentication_md5_password
{
    // The salt to use when encrypting the password.
    std::array<unsigned char, 4> salt;
};
boost::system::error_code parse(boost::span<const unsigned char> data, authentication_md5_password&);

// TODO: rest of authentication messages
// TODO: do we want to expose a fn to parse any authentication message? (they share message code, type is
// determined with the following bytes)

struct backend_key_data
{
    // The process ID of this backend.
    std::int32_t process_id;

    // The secret key of this backend.
    std::int32_t secret_key;
};
boost::system::error_code parse(boost::span<const unsigned char> data, backend_key_data& to);

// TODO: this should be at the end
// TODO: this is not the most efficient
using any_backend_message = boost::variant2::variant<
    authentication_ok,
    authentication_cleartext_password,
    authentication_md5_password,
    backend_key_data>;
boost::system::result<any_backend_message> parse(
    std::uint8_t message_type,
    boost::span<const unsigned char> data
);

struct bind_complete
{
};
inline boost::system::error_code parse(boost::span<const unsigned char> data, bind_complete&)
{
    return detail::check_empty(data);
}

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
    // The actual values. Contains a span<const unsigned char> per column,
    // containing the serialized value
    forward_parsing_view<boost::span<const unsigned char>> columns;
};
boost::system::error_code parse(boost::span<const unsigned char> data, data_row& to);

struct empty_query_response
{
};
inline boost::system::error_code parse(boost::span<const unsigned char> data, empty_query_response&)
{
    return detail::check_empty(data);
}

// Errors and notices. NoticeResponse has the same structure, and is represented with the same type
// TODO: should we make these const char*?
// All of the fields are optional, and user-defined functions may use them as they like,
// so we tolerate almost anything in them
struct error_response
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
boost::system::error_code parse(boost::span<const unsigned char> data, error_response& to);

struct no_data
{
};
inline boost::system::error_code parse(boost::span<const unsigned char> data, no_data&)
{
    return detail::check_empty(data);
}

struct notification_response
{
    // The process ID of the notifying backend process.
    std::int32_t process_id;

    // The name of the channel that the notify has been raised on.
    std::string_view channel_name;

    // The “payload” string passed from the notifying process.
    std::string_view payload;
};
boost::system::error_code parse(boost::span<const unsigned char> data, notification_response& to);

// A view over a serialized array of int32_t values
class int32_array_view
{
    const unsigned char* data_;
    std::size_t size_;  // number of items (as opposed to bytes)

    // Deserializes the int at that position, no check
    static std::int32_t deserialize_int(const unsigned char* ptr);

    std::int32_t unchecked_access(std::size_t i) const { return deserialize_int(data_ + 4 * i); }

public:
    class iterator
    {
        const unsigned char* data_;  // pointer into the message

        iterator(const unsigned char* data) noexcept : data_(data) {}
        std::int32_t dereference() const { return deserialize_int(data_); }
        void advance_by(std::ptrdiff_t i) { data_ += (4u * i); }

        friend class int32_array_view;

    public:
        using value_type = std::int32_t;
        using reference = std::int32_t;
        using pointer = std::int32_t;
        using difference_type = std::ptrdiff_t;
        using iterator_category = std::random_access_iterator_tag;

        iterator& operator++() noexcept
        {
            advance_by(1);
            return *this;
        }
        iterator operator++(int) noexcept
        {
            auto res = *this;
            advance_by(1);
            return res;
        }
        iterator& operator--() noexcept
        {
            advance_by(-1);
            return *this;
        }
        iterator operator--(int) noexcept
        {
            auto res = *this;
            advance_by(-1);
            return res;
        }
        iterator& operator+=(std::ptrdiff_t n) noexcept
        {
            advance_by(n);
            return *this;
        }
        iterator& operator-=(std::ptrdiff_t n) noexcept
        {
            advance_by(-n);
            return *this;
        }
        iterator operator+(std::ptrdiff_t n) const noexcept
        {
            iterator res(*this);
            res.advance_by(n);
            return res;
        }
        iterator operator-(std::ptrdiff_t n) const noexcept
        {
            iterator res(*this);
            res.advance_by(-n);
            return res;
        }
        std::ptrdiff_t operator-(iterator rhs) const noexcept { return (data_ - rhs.data_) / 4; }
        friend inline iterator operator+(std::ptrdiff_t n, iterator it) noexcept { return it + n; }

        pointer operator->() const noexcept { return dereference(); }
        reference operator*() const noexcept { return dereference(); }
        reference operator[](std::ptrdiff_t i) const noexcept
        {
            iterator temp(*this);
            temp.advance_by(i);
            return temp.dereference();
        }

        bool operator==(iterator rhs) const noexcept { return data_ == rhs.data_; }
        bool operator!=(iterator rhs) const noexcept { return !(*this == rhs); }
        bool operator<(iterator rhs) const noexcept { return data_ < rhs.data_; }
        bool operator<=(iterator rhs) const noexcept { return data_ <= rhs.data_; }
        bool operator>(iterator rhs) const noexcept { return data_ > rhs.data_; }
        bool operator>=(iterator rhs) const noexcept { return data_ >= rhs.data_; }
        // TODO: spaceship?
    };

    using const_iterator = iterator;
    using value_type = std::int32_t;
    using reference = value_type;
    using const_reference = value_type;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;

    // size is the number of int32 values. data is a pointer to the storage
    // where the ints are serialized. Performs no validation.
    // parse() performs this validation.
    // Don't use this unless you know what you're doing (TODO: make this private?)
    int32_array_view(const unsigned char* data, std::size_t size) noexcept : size_(size), data_(data) {}

    // The number of integer values
    std::size_t size() const { return size_; }

    // Is the range empty?
    bool empty() const { return size_ == 0u; }

    // TODO: should this be in a .cpp?
    std::int32_t at(std::size_t i) const
    {
        if (i >= size())
            BOOST_THROW_EXCEPTION(std::out_of_range("int32_array_view::at"));
        return unchecked_access(i);
    }

    std::int32_t operator[](std::size_t i) const noexcept
    {
        BOOST_ASSERT(i < size());
        return unchecked_access(i);
    }

    std::int32_t front() const noexcept { return (*this)[0]; }

    std::int32_t back() const noexcept { return (*this)[size() - 1]; }

    // Range functions
    iterator begin() const { return iterator(data_); }
    iterator end() const { return iterator(data_ + 4 * size_); }
};

// Describes the parameters of a statement
struct parameter_description
{
    int32_array_view parameters;
};
boost::system::error_code parse(boost::span<const unsigned char> data, parameter_description& to);

// Sent when a config parameter that might interest us changes value (e.g. character set)
struct parameter_status
{
    // The name of the run-time parameter being reported.
    std::string_view name;

    // The current value of the parameter.
    std::string_view value;
};
boost::system::error_code parse(boost::span<const unsigned char> data, parameter_status& to);

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
