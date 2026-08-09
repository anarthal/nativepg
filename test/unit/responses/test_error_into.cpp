//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/core/lightweight_test.hpp>

#include <cstddef>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "nativepg/client_errc.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/protocol/command_complete.hpp"
#include "nativepg/protocol/data_row.hpp"
#include "nativepg/protocol/notice_error.hpp"
#include "nativepg/request.hpp"
#include "nativepg/responses/error_into.hpp"
#include "nativepg/responses/response_handler.hpp"
#include "test_utils/printing.hpp"
#include "test_utils/response_msg_type.hpp"
#include "test_utils/test_range_eq.hpp"

using namespace nativepg;
using namespace nativepg::test;

namespace {

// State referenced by the mock handler in these tests.
// Keeps track of what the mocks do.
struct mock_state
{
    // Messages that on_message() was called with
    std::vector<on_msg_args> msgs;

    // What the handler reports on each successive on_message() call.
    // A missing entry means "no error"
    std::vector<extended_error> errors;

    // How many times the handler was copied/moved
    int copies{0};
    int moves{0};
};

struct mock_handler
{
    mock_state* st;

    explicit mock_handler(mock_state& state) noexcept : st(&state) {}
    mock_handler(const mock_handler& rhs) noexcept : st(rhs.st) { ++st->copies; }
    mock_handler(mock_handler&& rhs) noexcept : st(rhs.st) { ++st->moves; }
    mock_handler& operator=(const mock_handler&) = delete;
    mock_handler& operator=(mock_handler&&) = delete;
    ~mock_handler() = default;

    handler_setup_result setup(const request&, std::size_t offset) { return {offset + 1u}; }

    void on_message(const any_request_message& msg, std::size_t offset, extended_error& err)
    {
        const std::size_t i = st->msgs.size();
        st->msgs.push_back({to_type(msg), offset});
        if (i < st->errors.size())
            err = st->errors[i];
    }
};

// error_into is a valid handler
static_assert(response_handler<mock_handler>);
static_assert(response_handler<error_into<mock_handler>>);

// Some distinct errors to hand out
extended_error first_error() { return {client_errc::field_not_found, diagnostics(std::string("first"))}; }
extended_error second_error()
{
    return {client_errc::incompatible_field_type, diagnostics(std::string("second"))};
}

// Calls on_message and returns the produced error
template <response_handler Handler>
extended_error feed(Handler& h, const any_request_message& msg)
{
    extended_error err;
    h.on_message(msg, 42u, err);
    return err;
}

//
// Constructor decays
//
void test_ctor_handler_lvalue_ref()
{
    mock_state st;
    extended_error out;
    mock_handler inner{st};

    error_into wrapper{inner, out};

    // CTAD works correctly, and the handler was copied
    static_assert(std::is_same_v<decltype(wrapper), error_into<mock_handler>>);
    BOOST_TEST_EQ(st.copies, 1);
    BOOST_TEST_EQ(st.moves, 0);
}

void test_ctor_handler_const_lvalue_ref()
{
    mock_state st;
    extended_error out;
    const mock_handler inner{st};

    error_into wrapper{inner, out};

    // CTAD works correctly, and the handler was copied
    static_assert(std::is_same_v<decltype(wrapper), error_into<mock_handler>>);
    BOOST_TEST_EQ(st.copies, 1);
    BOOST_TEST_EQ(st.moves, 0);
}

void test_ctor_handler_rvalue()
{
    mock_state st;
    extended_error out;

    error_into wrapper{mock_handler{st}, out};

    // CTAD works correctly, and the handler was moved
    static_assert(std::is_same_v<decltype(wrapper), error_into<mock_handler>>);
    BOOST_TEST_EQ(st.copies, 0);
    BOOST_TEST_EQ(st.moves, 1);
}

//
// Copy and move construction
//
void test_copy_ctor()
{
    mock_state st;
    extended_error err;
    error_into original{mock_handler{st}, err};
    BOOST_TEST_EQ(st.copies, 0);
    BOOST_TEST_EQ(st.moves, 1);

    error_into copy{original};

    // The copy deduction candidate beats the deduction guide, so this is a plain
    // copy rather than a wrap: the type is not error_into<error_into<mock_handler>>
    static_assert(std::is_same_v<decltype(copy), error_into<mock_handler>>);
    BOOST_TEST_EQ(st.copies, 1);
    BOOST_TEST_EQ(st.moves, 1);

    // The copy drives the inner handler and still reports to the same output error
    request req;
    BOOST_TEST_EQ(copy.setup(req, 0u), handler_setup_result(1u));
    st.errors = {first_error()};
    BOOST_TEST_EQ(feed(copy, protocol::data_row{}), first_error());
    BOOST_TEST_EQ(err, first_error());

    const on_msg_args expected_msgs[] = {
        {response_msg_type::data_row, 42u},
    };
    test_range_eq(st.msgs, expected_msgs);
}

void test_move_ctor()
{
    mock_state st;
    extended_error out;
    error_into original{mock_handler{st}, out};
    BOOST_TEST_EQ(st.copies, 0);
    BOOST_TEST_EQ(st.moves, 1);

    error_into moved{std::move(original)};

    // Again a plain move, not a wrap
    static_assert(std::is_same_v<decltype(moved), error_into<mock_handler>>);
    BOOST_TEST_EQ(st.copies, 0);
    BOOST_TEST_EQ(st.moves, 2);

    request req;
    BOOST_TEST_EQ(moved.setup(req, 0u), handler_setup_result(1u));
    st.errors = {first_error()};
    BOOST_TEST_EQ(feed(moved, protocol::data_row{}), first_error());
    BOOST_TEST_EQ(out, first_error());

    const on_msg_args expected_msgs[] = {
        {response_msg_type::data_row, 42u},
    };
    test_range_eq(st.msgs, expected_msgs);
}

//
// Error capture
//
void test_nonerror_error()
{
    mock_state st;
    st.errors = {{}, second_error()};
    extended_error err;
    error_into handler{mock_handler{st}, err};

    // The 1st message succeeds, so nothing is captured
    BOOST_TEST_EQ(feed(handler, protocol::data_row{}), extended_error{});
    BOOST_TEST_EQ(err, extended_error{});

    // The 2nd one fails, and the error is set
    BOOST_TEST_EQ(feed(handler, protocol::command_complete{}), second_error());
    BOOST_TEST_EQ(err, second_error());

    // The inner handler saw both messages
    const on_msg_args expected_msgs[] = {
        {response_msg_type::data_row,         42u},
        {response_msg_type::command_complete, 42u},
    };
    test_range_eq(st.msgs, expected_msgs);
}

void test_error_nonerror()
{
    mock_state st;
    st.errors = {first_error()};
    extended_error err;
    error_into wrapper{mock_handler{st}, err};

    // The 1st message fails and sets
    BOOST_TEST_EQ(feed(wrapper, protocol::data_row{}), first_error());
    BOOST_TEST_EQ(err, first_error());

    // A later success doesn't clear what we captured
    BOOST_TEST_EQ(feed(wrapper, protocol::command_complete{}), extended_error{});
    BOOST_TEST_EQ(err, first_error());

    const on_msg_args expected_msgs[] = {
        {response_msg_type::data_row,         42u},
        {response_msg_type::command_complete, 42u},
    };
    test_range_eq(st.msgs, expected_msgs);
}

void test_error_error()
{
    mock_state st;
    st.errors = {first_error(), second_error()};
    extended_error err;
    error_into wrapper{mock_handler{st}, err};

    // The 1st error sets the output
    BOOST_TEST_EQ(feed(wrapper, protocol::data_row{}), first_error());
    BOOST_TEST_EQ(err, first_error());

    // The 2nd error is reported to the caller, but the first one is the one we keep
    BOOST_TEST_EQ(feed(wrapper, protocol::command_complete{}), second_error());
    BOOST_TEST_EQ(err, first_error());

    const on_msg_args expected_msgs[] = {
        {response_msg_type::data_row,         42u},
        {response_msg_type::command_complete, 42u},
    };
    test_range_eq(st.msgs, expected_msgs);
}

// Error messages are not special
void test_error_message()
{
    mock_state st;
    st.errors = {};  // The inner handler never reports errors
    extended_error out;
    error_into wrapper{mock_handler{st}, out};

    // If the inner handler doesn't report the error, we don't, either
    BOOST_TEST_EQ(feed(wrapper, protocol::error_response{}), extended_error{});
    BOOST_TEST_EQ(out, extended_error{});

    const on_msg_args expected_msgs[] = {
        {response_msg_type::error_response, 42u},
    };
    test_range_eq(st.msgs, expected_msgs);
}

void test_output_error_cleared_on_setup()
{
    mock_state st;
    extended_error err = first_error();  // left over from a previous run
    error_into wrapper{mock_handler{st}, err};

    request req;

    // setup() clears the output error and delegates to the inner handler
    BOOST_TEST_EQ(wrapper.setup(req, 2u), handler_setup_result(3u));
    BOOST_TEST_EQ(err, extended_error{});
}

}  // namespace

int main()
{
    // Constructor decays
    test_ctor_handler_lvalue_ref();
    test_ctor_handler_const_lvalue_ref();
    test_ctor_handler_rvalue();

    // Copy and move construction
    test_copy_ctor();
    test_move_ctor();

    // Error capture
    test_nonerror_error();
    test_error_nonerror();
    test_error_error();
    test_error_message();
    test_output_error_cleared_on_setup();

    return boost::report_errors();
}
