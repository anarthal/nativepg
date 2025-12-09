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
#include <iostream>
#include <source_location>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <vector>

#include "nativepg/parameter_ref.hpp"
#include "nativepg/protocol/bind.hpp"
#include "nativepg/protocol/common.hpp"
#include "nativepg/protocol/describe.hpp"
#include "nativepg/protocol/execute.hpp"
#include "nativepg/protocol/messages.hpp"
#include "nativepg/protocol/parse.hpp"
#include "nativepg/protocol/ready_for_query.hpp"
#include "nativepg/protocol/startup.hpp"
#include "nativepg/protocol/sync.hpp"
#include "nativepg/request.hpp"
#include "nativepg/response.hpp"

namespace asio = boost::asio;
using namespace nativepg;

static void check(boost::system::error_code ec, std::source_location loc = std::source_location::current())
{
    if (ec)
        boost::throw_with_location(boost::system::system_error(ec), loc);
}

struct myrow
{
    std::string_view f1;
};
BOOST_DESCRIBE_STRUCT(myrow, (), (f1))

int main()
{
    asio::io_context ctx;
    asio::ip::tcp::socket sock(ctx);

    // Connect
    sock.connect(asio::ip::tcp::endpoint(asio::ip::address_v4::loopback(), 5432));

    // Send login request
    std::vector<unsigned char> buffer;
    auto ec = protocol::serialize(
        protocol::startup_message{.user = "postgres", .database = "postgres", .params = {}},
        buffer
    );
    check(ec);
    asio::write(sock, asio::buffer(buffer));

    // Ignore all messages until a ready for query is received (TODO: short reads)
    buffer.resize(2048);
    auto size = sock.read_some(asio::buffer(buffer));
    buffer.resize(size);
    boost::span<const unsigned char> view{buffer};

    while (true)
    {
        // Message type
        if (view.empty())
            throw std::runtime_error("Not enough data for the message type");
        auto msg_type = view[0];
        view = view.subspan(1);
        if (msg_type == 'Z')
            break;  // ready for query

        // Size
        if (view.size() < 4u)
            throw std::runtime_error("Not enough data for the message length");
        auto l = boost::endian::load_big_s32(view.data());
        view = view.subspan(4u);
        if (l < 4)
            throw std::runtime_error("Bad length");
        l -= 4;
        if (view.size() < static_cast<std::size_t>(l))
            throw std::runtime_error("Not enough data for body");

        // Message
        view = view.subspan(l);
    }

    std::cout << "Handshake complete\n";

    // Compose our request
    request req;
    req.add_query("SELECT * FROM myt WHERE f1 <> $1", {"value2"});

    asio::write(sock, asio::buffer(req.payload()));

    size = sock.read_some(asio::buffer(buffer));
    buffer.resize(size);

    // Parse the response
    auto cb = resultset_callback<myrow>([](myrow r) { std::cout << "Got row: " << r.f1 << std::endl; });

    view = buffer;
    while (true)
    {
        // Message type
        if (view.empty())
            throw std::runtime_error("Not enough data for the message type");
        auto msg_type = view[0];
        view = view.subspan(1);

        // Size
        if (view.size() < 4u)
            throw std::runtime_error("Not enough data for the message length");
        auto l = boost::endian::load_big_s32(view.data());
        view = view.subspan(4u);
        if (l < 4)
            throw std::runtime_error("Bad length");
        l -= 4;
        if (view.size() < static_cast<std::size_t>(l))
            throw std::runtime_error("Not enough data for body");

        // Parse the message
        auto parse_result = protocol::parse(msg_type, view.subspan(0, l)).value();
        if (boost::variant2::holds_alternative<protocol::ready_for_query>(parse_result))
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
                    cb(any_request_message(msg));
                }
            },
            parse_result
        );

        // Message
        view = view.subspan(l);
    }

    std::cout << "Done\n";
}