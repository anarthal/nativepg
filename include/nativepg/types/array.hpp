//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_TYPES_ARRAY_HPP
#define NATIVEPG_TYPES_ARRAY_HPP

// PostgreSQL arrays <=> std::vector<T> (one-dimensional; parse and serialize) and std::span<const T>
// (one-dimensional; serialize). T can be any type with parse/serialize traits, including
// std::optional<U> for arrays with NULL elements.
//
// The functions in this header that take dimensions (*_nd) are the building blocks for arrays of any
// fixed number of dimensions, stored as a flat, row-major sequence of elements. They are used by
// nativepg/types/multi_array.hpp to support boost::multi::array.

#include <boost/endian/conversion.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "nativepg/client_errc.hpp"
#include "nativepg/field_traits.hpp"
#include "nativepg/field_view.hpp"
#include "nativepg/protocol/common.hpp"

namespace nativepg::types {

namespace detail {

struct array_oid_pair
{
    std::int32_t element;
    std::int32_t array;
};

// clang-format off
inline constexpr std::array<array_oid_pair, 24> array_oids{{
    {16, 1000},    // bool
    {17, 1001},    // bytea
    {18, 1002},    // char
    {19, 1003},    // name
    {20, 1016},    // int8
    {21, 1005},    // int2
    {23, 1007},    // int4
    {25, 1009},    // text
    {26, 1028},    // oid
    {114, 199},    // json
    {700, 1021},   // float4
    {701, 1022},   // float8
    {1042, 1014},  // bpchar
    {1043, 1015},  // varchar
    {1082, 1182},  // date
    {1083, 1183},  // time
    {1114, 1115},  // timestamp
    {1184, 1185},  // timestamptz
    {1186, 1187},  // interval
    {1266, 1270},  // timetz
    {1700, 1231},  // numeric
    {3802, 3807},  // jsonb
    {3908, 3909},  // tsrange
    {3910, 3911},  // tstzrange
}};
// clang-format on

inline constexpr std::size_t unset_dim = static_cast<std::size_t>(-1);

inline bool text_array_is_space(char c) noexcept
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

inline bool text_array_is_null(std::string_view s) noexcept
{
    constexpr std::string_view null_str = "NULL";
    if (s.size() != null_str.size())
        return false;
    for (std::size_t i = 0; i < s.size(); ++i)
    {
        const char c = (s[i] >= 'a' && s[i] <= 'z') ? static_cast<char>(s[i] - 'a' + 'A') : s[i];
        if (c != null_str[i])
            return false;
    }
    return true;
}

inline field_view make_array_element_view(std::string_view s) noexcept
{
    return field_view(
        std::span<const unsigned char>(reinterpret_cast<const unsigned char*>(s.data()), s.size())
    );
}

inline std::int32_t read_i32(const unsigned char* p) noexcept
{
    std::int32_t v;
    std::memcpy(&v, p, sizeof(v));
    return boost::endian::big_to_native(v);
}

inline void append_i32(std::vector<unsigned char>& to, std::int32_t v)
{
    v = boost::endian::native_to_big(v);
    const auto* p = reinterpret_cast<const unsigned char*>(&v);
    to.insert(to.end(), p, p + sizeof(v));
}

inline void write_i32(std::vector<unsigned char>& to, std::size_t pos, std::int32_t v) noexcept
{
    v = boost::endian::native_to_big(v);
    std::memcpy(to.data() + pos, &v, sizeof(v));
}

// Quotes (and escapes) the text element stored at to[pos, end) in place, if required.
inline void quote_text_element(std::vector<unsigned char>& to, std::size_t pos)
{
    const std::size_t len = to.size() - pos;
    const std::string_view sv(reinterpret_cast<const char*>(to.data() + pos), len);
    bool needs_quotes = len == 0 || text_array_is_null(sv);
    std::size_t escapes = 0;
    for (char c : sv)
    {
        if (c == '"' || c == '\\')
            ++escapes;
        if (c == '"' || c == '\\' || c == '{' || c == '}' || c == ',' || text_array_is_space(c))
            needs_quotes = true;
    }
    if (!needs_quotes)
        return;

    to.resize(to.size() + escapes + 2);
    std::size_t out = to.size();
    to[--out] = '"';
    for (std::size_t i = len; i-- > 0;)
    {
        const unsigned char c = to[pos + i];
        to[--out] = c;
        if (c == '"' || c == '\\')
            to[--out] = '\\';
    }
    to[pos] = '"';
}

// Parses a text array with a fixed number of dimensions into a flat vector
template <class T>
class text_array_parser
{
    std::string_view s_;
    std::size_t i_{0};
    std::int32_t element_oid_;
    std::span<std::size_t> dims_;
    std::vector<T>& flat_;
    std::string scratch_;  // only used for quoted elements

    void skip_space() noexcept
    {
        while (i_ < s_.size() && text_array_is_space(s_[i_]))
            ++i_;
    }

    std::error_code parse_element()
    {
        field_view elem;  // NULL by default
        if (s_[i_] == '"')
        {
            ++i_;
            scratch_.clear();
            bool closed = false;
            while (i_ < s_.size())
            {
                const char c = s_[i_++];
                if (c == '\\')
                {
                    if (i_ >= s_.size())
                        return client_errc::incomplete_message;
                    scratch_.push_back(s_[i_++]);
                }
                else if (c == '"')
                {
                    closed = true;
                    break;
                }
                else
                    scratch_.push_back(c);
            }
            if (!closed)
                return client_errc::incomplete_message;
            elem = make_array_element_view(scratch_);
        }
        else
        {
            const std::size_t start = i_;
            while (i_ < s_.size() && s_[i_] != ',' && s_[i_] != '}' && s_[i_] != '{')
                ++i_;
            if (i_ >= s_.size())
                return client_errc::incomplete_message;
            std::size_t end = i_;
            while (end > start && text_array_is_space(s_[end - 1]))
                --end;
            const auto raw = s_.substr(start, end - start);
            if (raw.empty())
                return client_errc::protocol_value_error;
            if (!text_array_is_null(raw))
                elem = make_array_element_view(raw);
        }

        T value{};
        if (auto ec = field_parse(elem, element_oid_, protocol::format_code::text, value))
            return ec;
        flat_.push_back(std::move(value));
        return {};
    }

    // s_[i_] is the opening brace of a sub-array at the given level
    std::error_code parse_level(std::size_t level)
    {
        ++i_;
        skip_space();
        std::size_t count = 0;
        const bool last = level + 1 == dims_.size();
        if (i_ < s_.size() && s_[i_] == '}')
            ++i_;
        else
        {
            while (true)
            {
                skip_space();
                if (i_ >= s_.size())
                    return client_errc::incomplete_message;
                if (last)
                {
                    if (s_[i_] == '{')
                        return client_errc::protocol_value_error;  // more dimensions than expected
                    if (auto ec = parse_element())
                        return ec;
                }
                else
                {
                    if (s_[i_] != '{')
                        return client_errc::protocol_value_error;  // fewer dimensions than expected
                    if (auto ec = parse_level(level + 1))
                        return ec;
                }
                ++count;

                skip_space();
                if (i_ >= s_.size())
                    return client_errc::incomplete_message;
                if (s_[i_] == ',')
                {
                    ++i_;
                    continue;
                }
                if (s_[i_] == '}')
                {
                    ++i_;
                    break;
                }
                return client_errc::protocol_value_error;
            }
        }

        // All sub-arrays at the same level must have the same size
        if (dims_[level] == unset_dim)
            dims_[level] = count;
        else if (dims_[level] != count)
            return client_errc::protocol_value_error;
        return {};
    }

public:
    text_array_parser(
        std::string_view s,
        std::int32_t element_oid,
        std::span<std::size_t> dims,
        std::vector<T>& flat
    )
        : s_(s), element_oid_(element_oid), dims_(dims), flat_(flat)
    {
    }

    std::error_code run()
    {
        flat_.clear();
        for (auto& d : dims_)
            d = unset_dim;

        // Skip optional dimension decoration, like [1:3]=
        if (!s_.empty() && s_.front() == '[')
        {
            const auto eq = s_.find('=');
            if (eq == std::string_view::npos)
                return client_errc::protocol_value_error;
            s_.remove_prefix(eq + 1);
        }

        skip_space();
        if (i_ >= s_.size() || s_[i_] != '{' || dims_.empty())
            return client_errc::protocol_value_error;
        if (auto ec = parse_level(0))
            return ec;
        skip_space();
        if (i_ != s_.size())
            return client_errc::extra_bytes;

        // An empty array has no sub-levels: every dimension is zero
        for (auto& d : dims_)
            if (d == unset_dim)
                d = 0;
        return {};
    }
};

template <class It>
std::error_code write_text_array_level(
    std::span<const std::size_t> dims,
    It& it,
    protocol::format_code code,
    std::vector<unsigned char>& to
)
{
    to.push_back('{');
    for (std::size_t n = 0; n < dims.front(); ++n)
    {
        if (n != 0)
            to.push_back(',');
        if (dims.size() > 1)
        {
            if (auto ec = write_text_array_level(dims.subspan(1), it, code, to))
                return ec;
        }
        else
        {
            const std::size_t pos = to.size();
            const auto ec = field_serialize(*it, code, to);
            ++it;
            if (ec == client_errc::serialize_null)
                to.insert(to.end(), {'N', 'U', 'L', 'L'});
            else if (ec)
                return ec;
            else
                quote_text_element(to, pos);
        }
    }
    to.push_back('}');
    return {};
}

}  // namespace detail

// Element OID => array OID. Returns 0 if the element type has no known array type.
constexpr std::int32_t array_oid_for_element(std::int32_t element_oid) noexcept
{
    for (const auto& p : detail::array_oids)
        if (p.element == element_oid)
            return p.array;
    return 0;
}

// Array OID => element OID. Returns 0 if the array type is unknown.
constexpr std::int32_t element_oid_for_array(std::int32_t array_oid) noexcept
{
    for (const auto& p : detail::array_oids)
        if (p.array == array_oid)
            return p.element;
    return 0;
}

// Binary format: int32 ndim, int32 has_null, int32 element oid, then ndim x (int32 size, int32 lower bound),
// then each element as int32 length (-1 for NULL) followed by its binary payload.
//
// Parses an array with exactly dims.size() dimensions (or an empty array, which has none on the wire)
// into a flat, row-major vector. dims receives the size of each dimension.
template <class T>
std::error_code parse_binary_array_nd(
    field_view from,
    std::int32_t array_oid,
    std::span<std::size_t> dims,
    std::vector<T>& flat
)
{
    const std::int32_t element_oid = element_oid_for_array(array_oid);
    const auto data = from.data();
    const std::size_t total = data.size();
    const std::size_t ndims = dims.size();
    flat.clear();
    for (auto& d : dims)
        d = 0;
    if (total < 12)
        return client_errc::incomplete_message;

    const std::int32_t ndim = detail::read_i32(data.data());
    const std::int32_t elem_type = detail::read_i32(data.data() + 8);
    if (ndim < 0 || elem_type != element_oid || (ndim != 0 && static_cast<std::size_t>(ndim) != ndims))
        return client_errc::protocol_value_error;
    if (ndim == 0)
        return total == 12 ? std::error_code() : client_errc::extra_bytes;

    std::size_t offset = 12 + 8 * ndims;
    if (total < offset)
        return client_errc::incomplete_message;

    // Every element takes at least 4 bytes, which protects us from bogus sizes
    const std::size_t max_elements = (total - offset) / 4;
    std::size_t count = 1;
    for (std::size_t d = 0; d < ndims; ++d)
    {
        const std::int32_t size = detail::read_i32(data.data() + 12 + 8 * d);
        if (size < 0)
            return client_errc::protocol_value_error;
        dims[d] = static_cast<std::size_t>(size);
        if (count != 0 && dims[d] > max_elements / count)
            return client_errc::protocol_value_error;
        count *= dims[d];
    }

    flat.reserve(count);
    for (std::size_t i = 0; i < count; ++i)
    {
        if (total - offset < 4)
            return client_errc::incomplete_message;
        const std::int32_t len = detail::read_i32(data.data() + offset);
        offset += 4;

        field_view elem;  // NULL by default
        if (len >= 0)
        {
            if (static_cast<std::size_t>(len) > total - offset)
                return client_errc::incomplete_message;
            elem = field_view(data.subspan(offset, static_cast<std::size_t>(len)));
            offset += static_cast<std::size_t>(len);
        }
        else if (len != -1)
            return client_errc::protocol_value_error;

        T value{};
        if (auto ec = field_parse(elem, element_oid, protocol::format_code::binary, value))
            return ec;
        flat.push_back(std::move(value));
    }
    return offset == total ? std::error_code() : client_errc::extra_bytes;
}

// Text format: {a,b,"c d",NULL}, nested for more dimensions. Quoted elements may contain backslash escapes.
template <class T>
std::error_code parse_text_array_nd(
    field_view from,
    std::int32_t array_oid,
    std::span<std::size_t> dims,
    std::vector<T>& flat
)
{
    return detail::text_array_parser<T>(from.data_str(), element_oid_for_array(array_oid), dims, flat).run();
}

// Appends an array with the given dimensions to `to`. values must contain exactly the product of dims
// elements, in row-major order. On error, `to` is restored to its original size.
template <class Range>
std::error_code serialize_array_nd(
    const Range& values,
    std::span<const std::size_t> dims,
    std::int32_t element_oid,
    protocol::format_code code,
    std::vector<unsigned char>& to
)
{
    const std::size_t original_size = to.size();
    auto fail = [&](std::error_code ec) {
        to.resize(original_size);
        return ec;
    };
    constexpr std::size_t max_i32 = static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max());

    bool empty = dims.empty();
    for (auto d : dims)
    {
        if (d > max_i32)
            return client_errc::value_too_big;
        empty = empty || d == 0;
    }

    if (code == protocol::format_code::binary)
    {
        const std::size_t flags_pos = original_size + 4;
        detail::append_i32(to, empty ? 0 : static_cast<std::int32_t>(dims.size()));
        detail::append_i32(to, 0);
        detail::append_i32(to, element_oid);
        if (empty)
            return {};
        for (auto d : dims)
        {
            detail::append_i32(to, static_cast<std::int32_t>(d));
            detail::append_i32(to, 1);
        }
        for (const auto& value : values)
        {
            const std::size_t len_pos = to.size();
            detail::append_i32(to, 0);
            const auto ec = field_serialize(value, code, to);
            if (ec == client_errc::serialize_null)
            {
                detail::write_i32(to, len_pos, -1);
                detail::write_i32(to, flags_pos, 1);
            }
            else if (ec)
                return fail(ec);
            else
            {
                const std::size_t len = to.size() - len_pos - 4;
                if (len > max_i32)
                    return fail(client_errc::value_too_big);
                detail::write_i32(to, len_pos, static_cast<std::int32_t>(len));
            }
        }
        return {};
    }

    if (empty)
    {
        to.push_back('{');
        to.push_back('}');
        return {};
    }
    auto it = std::begin(values);
    if (auto ec = detail::write_text_array_level(dims, it, code, to))
        return fail(ec);
    return {};
}

// One-dimensional convenience wrappers
template <class T>
std::error_code parse_binary_array(field_view from, std::int32_t array_oid, std::vector<T>& to)
{
    std::array<std::size_t, 1> dims{};
    return parse_binary_array_nd(from, array_oid, std::span<std::size_t>(dims), to);
}

template <class T>
std::error_code parse_text_array(field_view from, std::int32_t array_oid, std::vector<T>& to)
{
    std::array<std::size_t, 1> dims{};
    return parse_text_array_nd(from, array_oid, std::span<std::size_t>(dims), to);
}

template <class Range>
std::error_code serialize_array(
    const Range& values,
    std::int32_t element_oid,
    protocol::format_code code,
    std::vector<unsigned char>& to
)
{
    const std::array<std::size_t, 1> dims{
        static_cast<std::size_t>(std::distance(std::begin(values), std::end(values)))
    };
    return serialize_array_nd(values, std::span<const std::size_t>(dims), element_oid, code, to);
}

}  // namespace nativepg::types

// Registers parse/serialize traits for std::vector<T> and std::span<const T>. Included here (rather than
// force-included by field_traits.hpp) so that only TUs opting into this header pay for it.
#include "nativepg/detail/field_traits_array.hpp"

#endif  // NATIVEPG_TYPES_ARRAY_HPP
