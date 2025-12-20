//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/address_v4.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>
#include <boost/core/span.hpp>
#include <boost/describe/class.hpp>
#include <boost/endian/conversion.hpp>
#include <boost/system/detail/error_code.hpp>
#include <boost/system/system_error.hpp>
#include <boost/throw_exception.hpp>
#include <boost/variant2/variant.hpp>

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>

#include "nativepg/client_errc.hpp"
#include "nativepg/protocol/bind.hpp"
#include "nativepg/protocol/connection_state.hpp"
#include "nativepg/protocol/describe.hpp"
#include "nativepg/protocol/execute.hpp"
#include "nativepg/protocol/messages.hpp"
#include "nativepg/protocol/parse.hpp"
#include "nativepg/protocol/read_message_fsm.hpp"
#include "nativepg/protocol/ready_for_query.hpp"
#include "nativepg/protocol/startup_fsm.hpp"
#include "nativepg/request.hpp"
#include "nativepg/response.hpp"

namespace asio = boost::asio;
using namespace nativepg;

[[noreturn]]
static void die(boost::system::error_code ec, boost::source_location loc = BOOST_CURRENT_LOCATION)
{
    std::cerr << "Error: " << ec.what() << "\ncalled from " << loc << std::endl;
    exit(1);
}

struct myrow
{
    std::int32_t f3;
    std::string f1;
};
BOOST_DESCRIBE_STRUCT(myrow, (), (f3, f1))

std::span<const unsigned char> to_span(asio::const_buffer buff)
{
    return {static_cast<const unsigned char*>(buff.data()), buff.size()};
}

protocol::any_backend_message read_message(
    asio::ip::tcp::socket& sock,
    protocol::connection_state& st,
    protocol::read_message_stream_fsm& fsm
)
{
    std::size_t bytes_read = 0u;
    boost::system::error_code ec;

    while (true)
    {
        // Call the FSM
        auto res = fsm.resume(st, ec, bytes_read);

        switch (res.type())
        {
            case protocol::read_message_stream_fsm::result_type::error:
            {
                die(res.error());
            }
            case protocol::read_message_stream_fsm::result_type::message:
            {
                return res.message();
            }
            case protocol::read_message_stream_fsm::result_type::read:
            {
                bytes_read = sock.read_some(res.read_buffer(), ec);
                break;
            }
        }
    }
}

void startup(
    protocol::connection_state& st,
    protocol::read_message_stream_fsm& read_fsm,
    asio::ip::tcp::socket& sock
)
{
    protocol::startup_params params{.username = "postgres", .password = "secret", .database = "postgres"};
    protocol::startup_fsm fsm{params};
    protocol::any_backend_message msg;
    boost::system::error_code ec;

    while (true)
    {
        auto act = fsm.resume(st, ec, msg);
        switch (act.type())
        {
            case protocol::startup_fsm::result_type::done:
            {
                if (act.error())
                    die(act.error());
                return;
            }
            case protocol::startup_fsm::result_type::write:
            {
                asio::write(sock, act.write_data(), ec);
                break;
            }
            case protocol::startup_fsm::result_type::read:
            {
                msg = read_message(sock, st, read_fsm);
                break;
            }
        }
    }
}

int main()
{
    asio::io_context ctx;
    asio::ip::tcp::socket sock(ctx);
    protocol::connection_state st;
    protocol::read_message_stream_fsm read_fsm;

    // Connect
    sock.connect(asio::ip::tcp::endpoint(asio::ip::address_v4::loopback(), 5432));

    // Startup
    startup(st, read_fsm, sock);
    std::cout << "Startup complete\n";

    // Compose our request
    request req;
    req.add_query("SELECT * FROM myt WHERE f1 <> $1", {"value2"});

    asio::write(sock, asio::buffer(req.payload()));

    // Parse the response
    std::vector<myrow> vec;
    auto cb = into(vec);
    st.read_buffer.clear();

    while (true)
    {
        auto msg = read_message(sock, st, read_fsm);

        if (boost::variant2::holds_alternative<protocol::ready_for_query>(msg))
            break;
        boost::variant2::visit(
            [&cb](auto msg) {
                using T = decltype(msg);
                if constexpr (std::is_same_v<T, protocol::bind_complete> ||
                              std::is_same_v<T, protocol::close_complete> ||
                              std::is_same_v<T, protocol::command_complete> ||
                              std::is_same_v<T, protocol::data_row> ||
                              std::is_same_v<T, protocol::parameter_description> ||
                              std::is_same_v<T, protocol::row_description> ||
                              std::is_same_v<T, protocol::no_data> ||
                              std::is_same_v<T, protocol::empty_query_response> ||
                              std::is_same_v<T, protocol::portal_suspended> ||
                              std::is_same_v<T, protocol::error_response> ||
                              std::is_same_v<T, protocol::notice_response> ||
                              std::is_same_v<T, protocol::parse_complete>)
                {
                    auto ec = cb(any_request_message(msg));
                    if (ec && ec != client_errc::needs_more)
                        throw boost::system::system_error(ec, "Error in parser");
                }
            },
            msg
        );

        if (boost::variant2::holds_alternative<protocol::ready_for_query>(msg))
            break;
    }

    for (const auto& r : vec)
        std::cout << "Got row: " << r.f1 << ", " << r.f3 << std::endl;

    std::cout << "Done\n";
}