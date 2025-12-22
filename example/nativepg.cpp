//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/address_v4.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/write.hpp>
#include <boost/core/span.hpp>
#include <boost/describe/class.hpp>

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

#include "nativepg/protocol/connection_state.hpp"
#include "nativepg/protocol/messages.hpp"
#include "nativepg/protocol/read_message_fsm.hpp"
#include "nativepg/protocol/read_response_fsm.hpp"
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

protocol::any_backend_message read_message(asio::ip::tcp::socket& sock, protocol::connection_state& st)
{
    std::size_t bytes_read = 0u;
    boost::system::error_code ec;

    while (true)
    {
        // Call the FSM
        auto res = st.read_msg_stream_fsm.resume(st, ec, bytes_read);

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

void startup(protocol::connection_state& st, asio::ip::tcp::socket& sock)
{
    protocol::startup_params params{.username = "postgres", .password = "secret", .database = "postgres"};
    protocol::startup_fsm fsm{params};
    boost::system::error_code ec;
    std::size_t bytes_transferred = 0u;

    while (true)
    {
        auto act = fsm.resume(st, ec, bytes_transferred);
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
                bytes_transferred = asio::write(sock, act.write_data(), ec);
                break;
            }
            case protocol::startup_fsm::result_type::read:
            {
                bytes_transferred = sock.read_some(act.read_buffer(), ec);
                break;
            }
        }
    }
}

void read_response(
    protocol::connection_state& st,
    asio::ip::tcp::socket& sock,
    const request& req,
    protocol::response_handler_ref handler
)
{
    protocol::read_response_fsm fsm{req, handler};
    boost::system::error_code ec;
    std::size_t bytes_read = 0u;

    while (true)
    {
        auto res = fsm.resume(st, ec, bytes_read);
        switch (res.type())
        {
            case protocol::read_response_fsm::result_type::done:
            {
                if (res.error())
                    die(res.error());
                return;
            }
            case protocol::read_response_fsm::result_type::read:
            {
                bytes_read = sock.read_some(res.read_buffer(), ec);
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

    // Connect
    sock.connect(asio::ip::tcp::endpoint(asio::ip::address_v4::loopback(), 5432));

    // Startup
    startup(st, sock);
    std::cout << "Startup complete\n";

    // Compose our request
    request req;
    req.add_query("SELECT * FROM myt WHERE f1 <> $1", {"value2"});

    asio::write(sock, asio::buffer(req.payload()));

    // Parse the response
    std::vector<myrow> vec;
    auto cb = into(vec);
    read_response(st, sock, req, cb);

    for (const auto& r : vec)
        std::cout << "Got row: " << r.f1 << ", " << r.f3 << std::endl;

    std::cout << "Done\n";
}