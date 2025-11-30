//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_TYPES_PG_TYPE_TRAITS_HPP
#define NATIVEPG_TYPES_PG_TYPE_TRAITS_HPP

#include <cstdint>
#include <string_view>
#include <string>
#include <type_traits>
#include <cstddef>
#include <boost/core/span.hpp>
#include <boost/system/error_code.hpp>

#include "nativepg/types/pg_oid_type.hpp"


namespace nativepg {
namespace types {


template <typename T, pg_oid_type Oid>
struct pg_type_traits
{
    using value_type           = T;

    using read_span            = boost::span<const std::byte>;
    using write_span           = boost::span<std::byte>;

    static constexpr pg_oid_type oid_type = Oid;

    // -1 == variable length (common convention)
    static constexpr std::int32_t byte_len = -1;

    static constexpr std::string_view oid_name  = "unknown";
    static constexpr std::string_view type_name = "unknown";

    static constexpr bool supports_binary = false;

    // The specializations will implement these signatures:
    static boost::system::error_code parse_binary(read_span from, value_type& to);
    static boost::system::error_code parse_text(std::string_view from, value_type& to);

    static boost::system::error_code serialize_binary(value_type const& from, write_span& to);
    static boost::system::error_code serialize_text(value_type const& from, std::string& to);
};

template <pg_oid_type Oid>
struct pg_type_from_oid;   // primary template (no definition)


template <pg_oid_type Oid>
static constexpr auto make_pg_type()
{
    using T = typename pg_type_from_oid<Oid>::type;
    return pg_type_traits<T, Oid>{};
}



}
}


#endif
