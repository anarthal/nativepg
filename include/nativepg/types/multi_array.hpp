//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_TYPES_MULTI_ARRAY_HPP
#define NATIVEPG_TYPES_MULTI_ARRAY_HPP

// PostgreSQL arrays of any fixed number of dimensions <=> boost::multi::array<T, D> (parse and serialize).
// Requires Boost.Multi (<boost/multi/array.hpp>), so this header is opt-in.
//
// When parsing, the number of dimensions of the array sent by the server must match D (an empty array
// matches any D and yields an array with all its dimensions equal to zero). PostgreSQL arrays are
// rectangular, so every sub-array of a dimension has the same size. Lower bounds are ignored.
// When serializing, any dimension of size zero yields an empty PostgreSQL array.

#include <boost/multi/array.hpp>

namespace nativepg::detail {

template <class T, boost::multi::dimensionality_type D, std::size_t... I>
boost::multi::array<T, D> make_multi_array(
    const std::array<std::size_t, D>& dims,
    std::vector<T>& flat,
    std::index_sequence<I...>
)
{
    boost::multi::array<T, D> res(boost::multi::extents_t<D>(static_cast<boost::multi::ssize_t>(dims[I])...));
    std::move(flat.begin(), flat.end(), res.data_elements());
    return res;
}

template <class Array, std::size_t... I>
std::array<std::size_t, sizeof...(I)> get_multi_array_dims(const Array& arr, std::index_sequence<I...>)
{
    const auto sizes = arr.sizes();
    using std::get;
    return {static_cast<std::size_t>(get<I>(sizes))...};
}

}  // namespace nativepg::detail


// Registers parse/serialize traits for boost::multi::array<T, D>. Included here (rather than
// force-included by field_traits.hpp) so that only TUs opting into this header pay for it.
#include "nativepg/detail/field_traits_multi_array.hpp"

#endif  // NATIVEPG_TYPES_MULTI_ARRAY_HPP
