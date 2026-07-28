//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_RESPONSE_HPP
#define NATIVEPG_RESPONSE_HPP

#include <boost/assert.hpp>

#include <type_traits>

#include "nativepg/responses/response_handler.hpp"

namespace nativepg {

namespace detail {

template <class H0, class... HRest>
handler_setup_result response_setup(
    const request& req,
    std::size_t initial_offset,
    std::size_t* out_offsets,
    H0& h0,
    HRest&... hrest
)
{
    // Setup the first handler
    handler_setup_result h0_res = h0.setup(req, initial_offset);

    // Exit on failure
    if (h0_res.ec)
        return h0_res;

    // Store the offset
    *out_offsets = h0_res.offset;

    // Prevent infinite recursion
    if constexpr (sizeof...(HRest) == 0u)
    {
        return h0_res.offset;
    }
    else
    {
        // Recursively setup the rest of the handlers
        return response_setup(req, h0_res.offset, out_offsets + 1, hrest...);
    }
}

template <class H0, class... HRest>
const extended_error* response_get_result(const H0& h0, const HRest&... hrest)
{
    // Get the result for the first handler
    const extended_error& err = h0.result();

    // If it is an error, return this one
    if (err.code)
        return &err;

    // Prevent infinite recursion
    if constexpr (sizeof...(HRest) == 0u)
    {
        return nullptr;
    }
    else
    {
        // Recursively look for the other handlers
        return response_get_result(hrest...);
    }
}

}  // namespace detail

template <response_handler... Handlers>
class response
{
    static inline constexpr std::size_t N = sizeof...(Handlers);

    std::tuple<Handlers...> handlers_;
    std::array<std::size_t, N> offsets_{};
    std::size_t current_{};

public:
    template <class... Args>
        requires std::constructible_from<decltype(handlers_), Args&&...>
    explicit response(Args&&... args) : handlers_(std::forward<Args>(args)...)
    {
    }

    handler_setup_result setup(const request& req, std::size_t offset)
    {
        return std::apply(
            [this, &req, offset](auto&... h) {
                return detail::response_setup(req, offset, offsets_.data(), h...);
            },
            handlers_
        );
    }

    void on_message(const any_request_message& msg, std::size_t offset)
    {
        // Advance to the next element, if required
        if (offset >= offsets_[current_])
            ++current_;
        BOOST_ASSERT(offset < offsets_[current_]);

        // Hand the message to the appropriate handler
        boost::mp11::mp_with_index<N>(current_, [this, &msg, offset](auto I) {
            std::get<I>(handlers_).on_message(msg, offset);
        });
    }

    const extended_error& result() const
    {
        static_assert(N > 0);
        const auto* res = std::apply(
            [](const auto&... h) { return detail::response_get_result(h...); },
            handlers_
        );
        return res != nullptr ? *res : std::get<0>(handlers_).result();
    }

    const auto& handlers() const& { return handlers_; }
    auto& handlers() & { return handlers_; }
    auto&& handlers() && { return std::move(handlers_); }
};

template <class... Args>
response(Args&&...) -> response<std::decay_t<Args>...>;

}  // namespace nativepg

#endif
