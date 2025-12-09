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
#include <vector>

#include "nativepg/parameter_ref.hpp"
#include "nativepg/protocol/bind.hpp"
#include "nativepg/protocol/common.hpp"
#include "nativepg/protocol/describe.hpp"
#include "nativepg/protocol/execute.hpp"
#include "nativepg/protocol/parse.hpp"
#include "nativepg/protocol/startup.hpp"
#include "nativepg/protocol/sync.hpp"
#include "nativepg/request.hpp"

namespace asio = boost::asio;
using namespace nativepg;

static void check(boost::system::error_code ec, std::source_location loc = std::source_location::current())
{
    if (ec)
        boost::throw_with_location(boost::system::system_error(ec), loc);
}

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

    std::cout << "Done\n";

    // Now go send our messages
    request req(false);
    // req.add_simple_query("SELECT 1");
    statement<int> s1{"hola"};
    statement<std::string_view> s2{"adios"};

    req.add_prepare("SELECT * FROM myt WHERE f1 <> $1", s2)
        .add_bind(s2.bind("value2"))
        .add(protocol::describe{protocol::portal_or_statement::portal, {}})
        .add(protocol::execute{.portal_name = {}, .max_num_rows = 1})
        .add(protocol::execute{.portal_name = {}, .max_num_rows = 2})
        .add(protocol::sync{});

    asio::write(sock, asio::buffer(req.payload()));

    size = sock.read_some(asio::buffer(buffer));
    buffer.resize(size);
}