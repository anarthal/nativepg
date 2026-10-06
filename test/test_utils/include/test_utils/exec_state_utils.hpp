//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_TEST_EXEC_STATE_UTILS_HPP
#define NATIVEPG_TEST_EXEC_STATE_UTILS_HPP

#include <boost/assert/source_location.hpp>

#include "nativepg/exec_state.hpp"
#include "nativepg/write_status.hpp"

namespace nativepg::test {

// The values that exec_state's status accessors are expected to have
struct expected_status
{
    bool is_prepared{};
    write_status write_phase{write_status::request};
    bool reader_done{};
};

// A tool to check that exec_state's status accessors have the expected values
void check_status(
    const exec_state& exec_st,
    const expected_status& expected,
    boost::source_location loc = BOOST_CURRENT_LOCATION
);

}  // namespace nativepg::test

#endif
