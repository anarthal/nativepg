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
#include <memory>
#include <source_location>
#include <span>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <vector>

#include "nativepg/client_errc.hpp"
#include "nativepg/parameter_ref.hpp"
#include "nativepg/protocol/bind.hpp"
#include "nativepg/protocol/common.hpp"
#include "nativepg/protocol/describe.hpp"
#include "nativepg/protocol/execute.hpp"
#include "nativepg/protocol/messages.hpp"
#include "nativepg/protocol/parse.hpp"
#include "nativepg/protocol/parse_message_fsm.hpp"
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
    std::int32_t f3;
    std::string f1;
};
BOOST_DESCRIBE_STRUCT(myrow, (), (f3, f1))

std::span<const unsigned char> to_span(asio::const_buffer buff)
{
    return {static_cast<const unsigned char*>(buff.data()), buff.size()};
}

std::pair<protocol::any_backend_message, std::size_t> read_message(
    asio::ip::tcp::socket& sock,
    asio::dynamic_vector_buffer<unsigned char, std::allocator<unsigned char>>& buff
)
{
    protocol::parse_message_fsm fsm;

    while (true)
    {
        // Call the FSM
        auto res = fsm.resume(to_span(buff.data()));

        switch (res.type())
        {
            case protocol::parse_message_fsm::result_type::error:
            {
                std::cerr << "Error while parsing message: " << res.error() << std::endl;
                exit(1);
            }
            case protocol::parse_message_fsm::result_type::message:
            {
                return {res.message(), res.bytes_consumed()};
            }
            case protocol::parse_message_fsm::result_type::needs_more:
            {
                auto bytes_read = sock.read_some(buff.prepare(res.hint()));
                buff.commit(bytes_read);
                break;
            }
        }
    }
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

    // Ignore all messages until a ready for query is received
    std::vector<unsigned char> buff;
    auto dynbuff = asio::dynamic_buffer(buff);

    while (true)
    {
        auto [msg, bytes_consumed] = read_message(sock, dynbuff);
        if (boost::variant2::holds_alternative<protocol::ready_for_query>(msg))
            break;
        dynbuff.consume(bytes_consumed);
    }

    std::cout << "Handshake complete\n";

    // Compose our request
    request req;
    req.add_query("SELECT * FROM myt WHERE f1 <> $1", {"value2"});

    asio::write(sock, asio::buffer(req.payload()));

    // Parse the response
    std::vector<myrow> vec;
    auto cb = into(vec);
    dynbuff.consume(dynbuff.max_size());

    while (true)
    {
        auto [msg, bytes_consumed] = read_message(sock, dynbuff);

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

        dynbuff.consume(bytes_consumed);
    }

    for (const auto& r : vec)
        std::cout << "Got row: " << r.f1 << ", " << r.f3 << std::endl;

    std::cout << "Done\n";
}