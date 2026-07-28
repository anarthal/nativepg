//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_PARAMETER_REF_HPP
#define NATIVEPG_PARAMETER_REF_HPP

#include <boost/system/error_code.hpp>

#include <cstdint>

#include "nativepg/field_traits.hpp"

namespace nativepg {

class parameter_ref
{
    using serialize_fn = void (*)(const void* param, std::vector<unsigned char>& buffer);

    template <class T>
    static void do_serialize_text(const void* param, std::vector<unsigned char>& buffer)
    {
        field_serialize_text(*static_cast<const T*>(param), buffer);
    }

    template <class T>
    static void do_serialize_binary(const void* param, std::vector<unsigned char>& buffer)
    {
        field_serialize_binary(*static_cast<const T*>(param), buffer);
    }

    const void* value_;
    serialize_fn text_;
    serialize_fn binary_;
    std::int32_t oid_;

public:
    template <serializable_field T>
    parameter_ref(const T& value) noexcept
        : value_(&value),
          text_(&do_serialize_text<T>),
          binary_(&do_serialize_binary<T>()),
          oid_(field_serialize_oid<T>)
    {
    }

    std::int32_t type_oid() const { return oid_; }
    void serialize_text(std::vector<unsigned char>& buffer) const { text_(value_, buffer); }
    void serialize_binary(std::vector<unsigned char>& buffer) const { binary_(value_, buffer); }
};

}  // namespace nativepg

#endif
