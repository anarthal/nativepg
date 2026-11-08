//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_DETAIL_FIELD_TRAITS_MULTI_ARRAY_HPP
#define NATIVEPG_DETAIL_FIELD_TRAITS_MULTI_ARRAY_HPP

// This header is opt-in: it's included by nativepg/types/multi_array.hpp, which is itself opt-in.

#include <boost/multi/array.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <system_error>
#include <utility>
#include <vector>

#include "nativepg/client_errc.hpp"
#include "nativepg/field_traits.hpp"
#include "nativepg/field_view.hpp"
#include "nativepg/protocol/common.hpp"
#include "nativepg/types/array.hpp"

namespace nativepg {

// --- Parse
// PostgreSQL array => boost::multi::array<T, D>
template <parsable_field T, boost::multi::dimensionality_type D>
    requires(D >= 1)
struct parse_field_traits<boost::multi::array<T, D>>
{
    static std::error_code is_compatible(std::int32_t type_oid)
    {
        const auto element_oid = types::element_oid_for_array(type_oid);
        if (element_oid == 0)
            return client_errc::incompatible_field_type;
        return field_is_compatible<T>(element_oid);
    }

    static std::error_code parse(
        field_view from,
        std::int32_t type_oid,
        protocol::format_code code,
        boost::multi::array<T, D>& to
    )
    {
        if (from.is_null())
            return client_errc::unexpected_null;

        std::array<std::size_t, D> dims{};
        std::vector<T> flat;
        const auto ec = code == protocol::format_code::binary
                            ? types::parse_binary_array_nd(from, type_oid, std::span<std::size_t>(dims), flat)
                            : types::parse_text_array_nd(from, type_oid, std::span<std::size_t>(dims), flat);
        if (ec)
            return ec;
        to = detail::make_multi_array<T, D>(dims, flat, std::make_index_sequence<D>{});
        return {};
    }
};

// --- Serialize
// boost::multi::array<T, D> => PostgreSQL array
template <serializable_field T, boost::multi::dimensionality_type D>
    requires(D >= 1)
struct serialize_field_traits<boost::multi::array<T, D>>
{
    static_assert(
        types::array_oid_for_element(field_serialize_oid<T>) != 0,
        "The element type has no known PostgreSQL array type"
    );

    static constexpr std::int32_t oid = types::array_oid_for_element(field_serialize_oid<T>);

    static std::error_code serialize(
        const boost::multi::array<T, D>& value,
        protocol::format_code code,
        std::vector<unsigned char>& to
    )
    {
        const auto dims = detail::get_multi_array_dims(value, std::make_index_sequence<D>{});
        return types::serialize_array_nd(
            value.elements(),
            std::span<const std::size_t>(dims),
            field_serialize_oid<T>,
            code,
            to
        );
    }
};

}  // namespace nativepg

#endif  // NATIVEPG_DETAIL_FIELD_TRAITS_MULTI_ARRAY_HPP
