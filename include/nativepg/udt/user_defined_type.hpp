//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
//

#ifndef NATIVEPG_UDT_USER_DEFINED_TYPE_HPP
#define NATIVEPG_UDT_USER_DEFINED_TYPE_HPP

#include <boost/system/error_code.hpp>

#include <any>

#include "nativepg/client_errc.hpp"
#include "nativepg/field_view.hpp"
#include "nativepg/protocol/describe.hpp"
#include "nativepg/types/registry.hpp"
#include "nativepg/detail/field_traits.hpp"

namespace nativepg::udt {


template <typename FieldType>
class user_defined_type
{
protected:
    types::type_info db_ti_;

public:
    explicit user_defined_type(const types::type_info& ti) : db_ti_(ti) {}
    virtual ~user_defined_type() = default;


    boost::system::error_code is_compatible(const protocol::field_description&)
    {
        return client_errc::incompatible_field_type;
    }

    boost::system::error_code parse(
        const field_view,
        const protocol::field_description&,
        FieldType&)
    {
        return client_errc::incompatible_field_type;
    }
};

template <typename FieldType>
auto make_udt(const types::type_info& ti)
{
    return std::make_unique<user_defined_type<FieldType>>(ti);
}

}  // namespace nativepg::udt


namespace nativepg::detail {

template <typename FieldType>
struct field_is_compatible
{
    static boost::system::error_code call(const protocol::field_description& desc, const std::optional<types::type_info>& ti = std::nullopt)
    {
        auto udt = udt::make_udt<FieldType>(ti.value_or(types::type_info{}));
        return udt->is_compatible(desc);
    }
};

template <typename FieldType>
struct field_parse
{
    static boost::system::error_code call(
        const field_view& from,
        const protocol::field_description& desc,
        FieldType& to,
        const std::optional<types::type_info>& ti = std::nullopt
    )
    {
        auto udt = udt::make_udt<FieldType>(ti.value_or(types::type_info{}));
        return udt->parse(from, desc, to);
    }
};

} // namespace nativepg::detail

#endif  // NATIVEPG_UDT_USER_DEFINED_TYPE_HPP
