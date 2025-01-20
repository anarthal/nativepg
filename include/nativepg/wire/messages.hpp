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
#include <string_view>

namespace nativepg {
namespace protocol {

namespace detail {
boost::system::error_code check_empty(boost::span<const unsigned char> data);
}

// Message header operations
struct message_header
{
    std::uint8_t type;  // The message type
    std::int32_t size;  // Should be >= 0
};

inline void serialize_header(message_header header, boost::span<unsigned char, 5> dest)
{
    unsigned char* ptr = dest.data();
    *ptr++ = header.type;
    boost::endian::store_big_s32(ptr, header.size);
}

inline message_header parse_header(boost::span<const unsigned char, 5> from)
{
    return {from[0], boost::endian::load_big_s32(from.data() + 1u)};
}

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

// View over the columns of a DataRow message. Each element is a serialized field (a byte string).
// This collection assumes the data is well-formed. parse() checks this.
class columns_view
{
    std::size_t size_;                       // number of fields
    boost::span<const unsigned char> data_;  // serialized fields

public:
    class iterator
    {
        const unsigned char* data_;  // pointer into the message

        iterator(const unsigned char* data) noexcept : data_(data) {}
        boost::span<const unsigned char> dereference() const;
        void advance();

        friend class columns_view;

    public:
        using value_type = boost::span<const unsigned char>;
        using reference = boost::span<const unsigned char>;
        using pointer = boost::span<const unsigned char>;
        using difference_type = std::ptrdiff_t;
        using iterator_category = std::forward_iterator_tag;

        iterator& operator++() noexcept
        {
            advance();
            return *this;
        }
        iterator operator++(int) noexcept
        {
            auto res = *this;
            advance();
            return res;
        }
        reference operator*() const noexcept { return dereference(); }
        bool operator==(iterator rhs) const noexcept { return data_ == rhs.data_; }
        bool operator!=(iterator rhs) const noexcept { return !(*this == rhs); }
    };

    using const_iterator = iterator;
    using value_type = boost::span<const unsigned char>;
    using reference = value_type;
    using const_reference = value_type;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;

    // size is the number of fields. data is a view over the "fields" part
    // of the DataRow message, and must be valid. parse() performs this validation.
    // Don't use this unless you know what you're doing (TODO: make this private?)
    columns_view(std::size_t size, boost::span<const unsigned char> data) noexcept : size_(size), data_(data)
    {
    }

    // The number of fields
    std::size_t size() const { return size_; }

    // Is the range empty?
    bool empty() const { return size_ == 0u; }

    // Range functions
    iterator begin() const { return iterator(data_.begin()); }
    iterator end() const { return iterator(data_.end()); }
};

struct data_row
{
    columns_view columns;
};
boost::system::error_code parse(boost::span<const unsigned char> data, data_row& to);

struct empty_query_response
{
};
inline boost::system::error_code parse(boost::span<const unsigned char> data, empty_query_response&)
{
    return detail::check_empty(data);
}



}  // namespace protocol
}  // namespace nativepg

#endif
