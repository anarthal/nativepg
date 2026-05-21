//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_SRC_NATIVEPG_INTERNAL_SCRAM_SHA256_MESSAGES_HPP
#define NATIVEPG_SRC_NATIVEPG_INTERNAL_SCRAM_SHA256_MESSAGES_HPP

#include <boost/core/span.hpp>
#include <boost/endian/conversion.hpp>
#include <boost/system/error_code.hpp>
#include <boost/system/result.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>
#include <system_error>
#include <vector>

#include "nativepg/client_errc.hpp"
#include "nativepg/protocol/detail/serialization_context.hpp"
#include "nativepg_internal/base64.hpp"

// Message formats described in https://datatracker.ietf.org/doc/html/rfc5802

namespace nativepg::protocol::detail::scram_sha256 {

// This is really a password message. serialize serializes the entire message,
// including the header. Returns the client-first-message-bare part of the serialized
// message, required by the SCRAM algorithm
struct scram_sha256_client_first_message
{
    // The SASL mechanism name that was chosen
    std::string_view mechanism;

    // The nonce sent to the server
    std::string_view nonce;
};

[[nodiscard]] inline boost::system::result<boost::span<const unsigned char>> serialize(
    const scram_sha256_client_first_message& msg,
    std::vector<unsigned char>& to
)
{
    ::nativepg::protocol::detail::serialization_context ctx(to);

    // Header
    ctx.add_header('p');

    // SASL mechanism name
    ctx.add_string(msg.mechanism);

    // The rest of the message is a SCRAM-SHA256 client-first-message.
    // It's preceeded by a 4 byte integer indicating its length.
    // Reserve empty space for this size
    std::size_t length_offset = to.size();
    ctx.add_bytes(std::array<unsigned char, 4>{});

    // client-first-message = gs2-header client-first-message-bare
    // gs2-header      = gs2-cbind-flag "," [ authzid ] ","
    // client-first-message-bare =
    //      [reserved-mext ","] username "," nonce ["," extensions]
    // username        = "n=" saslname ;; always empty in our case, sent in the startup msg
    // nonce           = "r=" c-nonce [s-nonce] ;; printable
    // printable       = %x21-2B / %x2D-7E

    // Add the gs2-header (always "n,," because we don't support channel binding yet)
    ctx.add_bytes("n,,");

    // client-first-message-bare starts here
    std::size_t bare_start_offset = to.size();

    // Add the username (always empty) and the nonce
    // TODO: should we check that the nonce complies with the grammar?
    ctx.add_bytes("n=,r=");
    ctx.add_bytes(msg.nonce);

    // client-first-message-bare ends here
    std::size_t bare_end_offset = to.size();

    // Calculate the length of what we serialized
    auto data_length = to.size() - length_offset - 4u;
    if (data_length > (std::numeric_limits<std::int32_t>::max)())
    {
        ctx.add_error(::nativepg::client_errc::value_too_big);
    }
    else
    {
        boost::endian::store_big_s32(to.data() + length_offset, static_cast<std::int32_t>(data_length));
    }

    // Finalize
    if (auto ec = ctx.finalize_message())
        return ec;

    // Done
    return boost::span<const unsigned char>(to.data() + bare_start_offset, to.data() + bare_end_offset);
}

// This is an authentication_sasl_continue message. parse does not parse the type
struct scram_sha256_server_first_message
{
    // The nonce sent by the server, should contain the nonce we sent and an extra value created by the server
    std::string_view nonce;

    // The salt sent by the server. This comes as base64
    // TODO: salt is likely restricted in size, so we could use a static_vector and make this non-allocating
    std::vector<unsigned char> salt;

    // Iteration count. As an arbitrary fixed-width integer to set an upper limit
    std::uint32_t iteration_count;
};

inline bool scram_is_printable(unsigned char c)
{
    return (c >= 0x21 && c <= 0x2b) || (c >= 0x2d && c <= 0x7e);
}

[[nodiscard]] inline boost::system::error_code parse(
    boost::span<const unsigned char> data,
    scram_sha256_server_first_message& to
)
{
    // server-first-message = [reserved-mext ","] nonce "," salt "," iteration-count ["," extensions]
    // reserved-mext  = "m=" 1*(value-char) ;; if this is present, we're missing extensions and should fail
    // nonce          = "r=" c-nonce [s-nonce] ;; these are equal to printable
    // printable       =%x21-2B / %x2D-7E
    // salt            = "s=" base64
    // iteration-count = "i=" posit-number
    // extensions = attr-val *("," attr-val) ;; to be ignored
    // attr-val        = ALPHA "=" value
    // value           = 1*value-char

    const unsigned char* p = data.data();
    const unsigned char* last = data.data() + data.size();

    // Try to match reserved-mext
    if (p != last && *p == static_cast<unsigned char>('m'))
    {
        ++p;
        return (p == last || *p != ',') ? ::nativepg::client_errc::invalid_scram_message
                                        : ::nativepg::client_errc::mandatory_scram_extension_not_supported;
    }

    // Parse the nonce
    if (p == last || *p++ != 'r')
        return ::nativepg::client_errc::invalid_scram_message;
    if (p == last || *p++ != '=')
        return ::nativepg::client_errc::invalid_scram_message;
    const auto* nonce_first = p;
    while (true)
    {
        if (p == last)
            return ::nativepg::client_errc::invalid_scram_message;
        if (*p == ',')
            break;
        if (!scram_is_printable(*p))
            return ::nativepg::client_errc::invalid_scram_message;
        ++p;
    }
    to.nonce = {reinterpret_cast<const char*>(nonce_first), reinterpret_cast<const char*>(p)};
    ++p;  // skip the final comma

    // Parse the salt
    if (p == last || *p++ != 's')
        return ::nativepg::client_errc::invalid_scram_message;
    if (p == last || *p++ != '=')
        return ::nativepg::client_errc::invalid_scram_message;
    const auto* salt_first = p;
    while (true)
    {
        if (p == last)
            return ::nativepg::client_errc::invalid_scram_message;
        if (*p == ',')
            break;
        ++p;
    }
    if (auto ec = ::nativepg::protocol::detail::base64_decode({salt_first, p}, to.salt))
        return ec;
    ++p;  // skip the final comma

    // Parse the iteration count. Verify that all the characters
    // are numbers, to avoid any possible signed char
    if (p == last || *p++ != 'i')
        return ::nativepg::client_errc::invalid_scram_message;
    if (p == last || *p++ != '=')
        return ::nativepg::client_errc::invalid_scram_message;
    const char* i_first = reinterpret_cast<const char*>(p);
    const char* i_last = std::find(i_first, reinterpret_cast<const char*>(last), ',');
    auto parse_result = std::from_chars(i_first, i_last, to.iteration_count);
    if (parse_result.ec != std::errc() || parse_result.ptr != i_last)
        return ::nativepg::client_errc::invalid_scram_message;

    // TODO: verify that if we got any extensions, they are well-formed
    return {};
}

// client_final_message is a password message. It needs to be serialized in two parts.
//   * serialize_without_proof serializes the header, and all the message except for the proof.
//     It returns client-final-message-without-proof, required by the SCRAM algorithm.
//   * serialize_proof serializes the proof and adjusts the header.
//     Computing proof needs client-final-message-without-proof. This is why we need to split serialization.
class scram_sha256_client_final_message_serializer
{
    ::nativepg::protocol::detail::serialization_context ctx_;

public:
    scram_sha256_client_final_message_serializer(std::vector<unsigned char>& to) noexcept : ctx_(to) {}

    // Should be called once, first
    [[nodiscard]] boost::system::result<std::span<const unsigned char>> serialize_without_proof(
        std::string_view nonce
    )
    {
        // Header
        ctx_.add_header('p');

        // client-final-message = client-final-message-without-proof "," proof
        // client-final-message-without-proof = channel-binding "," nonce ["," extensions]
        // channel-binding = "c=" base64 ;; base64 encoding of cbind-input.
        // cbind-input   = gs2-header [ cbind-data ] ;; cbind-data absent if no channel binding is present
        // gs2-header      = gs2-cbind-flag "," [ authzid ] ","
        // gs2-cbind-flag  = ("p=" cb-name) / "n" / "y" ;; always "n" in our case
        // nonce           = "r=" c-nonce [s-nonce]
        // extensions = attr-val *("," attr-val) ;; none supported right now
        // proof           = "p=" base64

        // client-final-message-without-proof starts here
        std::size_t offset_first = ctx_.buffer().size();

        // gs2-header is always "n,," when no channel binding is present
        // this makes channel-binding always equals to "c=biws"
        // nonce ("r=") comes next
        ctx_.add_bytes("c=biws,r=");
        ctx_.add_bytes(nonce);

        // client-final-message-without-proof ends here
        std::size_t offset_last = ctx_.buffer().size();

        // proof depends on client-final-message-without-proof, so it's serialized by a downstream function

        // Check for errors
        if (auto ec = ctx_.error())
            return ec;

        // Done
        const auto* data = ctx_.buffer().data();
        return std::span<const unsigned char>(data + offset_first, data + offset_last);
    }

    // Should be called once, after serialize_without_proof
    [[nodiscard]] boost::system::error_code serialize_proof(std::span<const unsigned char> proof)
    {
        // proof
        ctx_.add_bytes(",p=");
        ::nativepg::protocol::detail::base64_encode(proof, ctx_.buffer());

        // Finalize message
        return ctx_.finalize_message();
    }
};

// This is an authentication_sasl_final message. parse does not parse the type
struct scram_sha256_server_final_message
{
    // Proof that the server had access to our password
    // TODO: this is restricted to digest size, we should probably use this knowledge and static storage
    std::vector<unsigned char> server_signature;
};

[[nodiscard]] inline boost::system::error_code parse(
    boost::span<const unsigned char> data,
    scram_sha256_server_final_message& to
)
{
    // server-final-message = (server-error / verifier) ["," extensions]
    // server-error = "e=" server-error-value ;; TODO: does postgres really ever send this?
    // verifier        = "v=" base64 ;; base-64 encoded ServerSignature.
    // extensions = attr-val *("," attr-val)

    const char* p = reinterpret_cast<const char*>(data.data());
    const char* last = p + data.size();

    // TODO: handle server-error

    // Parse the verifier
    if (p == last || *p++ != 'v')
        return ::nativepg::client_errc::invalid_scram_message;
    if (p == last || *p++ != '=')
        return ::nativepg::client_errc::invalid_scram_message;
    const char* verifier_last = std::find(p, last, ',');
    if (auto ec = ::nativepg::protocol::detail::base64_decode(
            boost::span<const unsigned char>(
                reinterpret_cast<const unsigned char*>(p),
                reinterpret_cast<const unsigned char*>(verifier_last)
            ),
            to.server_signature
        ))
        return ec;

    // TODO: verify that extensions are well-formed
    return {};
}

}  // namespace nativepg::protocol::detail::scram_sha256

#endif
