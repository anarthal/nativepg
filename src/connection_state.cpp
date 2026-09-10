//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include "nativepg/protocol/any_backend_message.hpp"
#include "nativepg/protocol/async.hpp"
#include "nativepg/protocol/connection_state.hpp"
#include "nativepg/protocol/parse_encoding.hpp"

using namespace nativepg::protocol;

void connection_state::update_tracked(const any_backend_message& msg)
{
    switch (msg.type())
    {
        case any_backend_message::kind::backend_key_data:
        {
            const auto& key = msg.get_backend_key_data();
            backend_process_id = key.process_id;
            backend_secret_key = key.secret_key;
            return;
        }
        case any_backend_message::kind::parameter_status:
        {
            const auto& param = msg.get_parameter_status();

            // Any value we don't understand leaves the GUC as unknown, rather than
            // guessing: escaping with the wrong assumption is a correctness problem.
            if (param.name == "standard_conforming_strings")
            {
                if (param.value == "on")
                    standard_conforming_strings = true;
                else if (param.value == "off")
                    standard_conforming_strings = false;
                else
                    standard_conforming_strings.reset();
            }
            else if (param.name == "client_encoding")
            {
                client_encoding = parse_encoding(param.value);
            }
            return;
        }
        default: return;
    }
}
