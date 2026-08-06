//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_PARAMETER_REF_HPP
#define NATIVEPG_PARAMETER_REF_HPP

#include <boost/endian/conversion.hpp>

#include <cstdint>

#include "nativepg/field_traits.hpp"

namespace nativepg {

class parameter_ref
{
    using serialize_fn = boost::system::error_code (*)(const void* param, std::vector<unsigned char>& buffer);

    template <class T>
    static boost::system::error_code do_serialize_text(const void* param, std::vector<unsigned char>& buffer)
    {
        return field_serialize_text(*static_cast<const T*>(param), buffer);
    }

    template <class T>
    static boost::system::error_code do_serialize_binary(
        const void* param,
        std::vector<unsigned char>& buffer
    )
    {
        return field_serialize_binary(*static_cast<const T*>(param), buffer);
    }

    const void* value_;
    serialize_fn text_;
    serialize_fn binary_;
    std::int32_t oid_;

public:
    template <class T>
        requires(!std::same_as<T, parameter_ref>)
    parameter_ref(const T& value) noexcept
        : value_(&value),
          text_(&do_serialize_text<T>),
          binary_(&do_serialize_binary<T>),
          oid_(field_serialize_oid<T>)
    {
        static_assert(serializable_field<T>);  // TODO: could we make the error messages clearer here?
    }

    std::int32_t type_oid() const { return oid_; }

    boost::system::error_code serialize_text(std::vector<unsigned char>& buffer) const
    {
        return text_(value_, buffer);
    }

    boost::system::error_code serialize_binary(std::vector<unsigned char>& buffer) const
    {
        return binary_(value_, buffer);
    }
};

}  // namespace nativepg

#endif
