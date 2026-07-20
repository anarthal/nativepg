//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/core/lightweight_test.hpp>

#include <span>

#include "nativepg/field_view.hpp"
#include "test_utils/test_utils.hpp"

using namespace nativepg;

namespace {

// Default ctor builds a NULL value. data() returns an empty span
void test_default_constructor()
{
    field_view f;
    BOOST_TEST(f.is_null());
    NATIVEPG_TEST_CONT_EQ(f.data(), std::span<const unsigned char>{});
}

// Constructor from actual data
void test_constructor_from_data()
{
    const unsigned char data[] = {1, 2, 3, 4};
    field_view f{std::span<const unsigned char>(data)};
    BOOST_TEST(!f.is_null());
    NATIVEPG_TEST_CONT_EQ(f.data(), data);
}

// Constructor from a default-constructed span:
// interpreted as an empty value, not as a NULL
void test_constructor_from_default_span()
{
    field_view f{std::span<const unsigned char>{}};
    BOOST_TEST(!f.is_null());
    NATIVEPG_TEST_CONT_EQ(f.data(), std::span<const unsigned char>{});
}

// Constructor from a span with zero size but (possibly) non-null data():
// interpreted as an empty value, not as a NULL
// (sanity check, we don't have data != nullptr as a NULL indicator)
void test_constructor_from_empty_nonnull_span()
{
    const unsigned char data[] = {1, 2, 3, 4};
    std::span<const unsigned char> empty_nonnull(data, 0u);
    field_view f{empty_nonnull};
    BOOST_TEST(!f.is_null());
    NATIVEPG_TEST_CONT_EQ(f.data(), std::span<const unsigned char>{});
}

// Functions can be called in a constexpr context.
// field_view does not work in constexpr contexts with sanitizers.
#ifndef __SANITIZE_ADDRESS__
static_assert(field_view().is_null());
static_assert(!field_view(std::span<const unsigned char>{}).is_null());
static_assert(field_view().data().empty());
#endif

}  // namespace

int main()
{
    test_default_constructor();
    test_constructor_from_data();
    test_constructor_from_default_span();
    test_constructor_from_empty_nonnull_span();

    return boost::report_errors();
}