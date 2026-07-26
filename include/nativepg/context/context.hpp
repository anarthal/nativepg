//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_CONTEXT_CONTEXT_HPP
#define NATIVEPG_CONTEXT_CONTEXT_HPP

#include <concepts>
#include <memory>
#include <span>

#include "nativepg/connect_params.hpp"
#include "nativepg/dynamic_resultset.hpp"
#include "nativepg/extended_error.hpp"
#include "nativepg/protocol/startup_fsm.hpp"
#include "nativepg/request.hpp"
#include "nativepg/response.hpp"

namespace nativepg::context {

    // --- Polymorphic Heap-Allocated State Base ---
    struct context_state {
        virtual ~context_state() = default;
        [[nodiscard]] virtual const void* get_type_id() const noexcept = 0;
    };

    template <typename Tag>
    struct context_state_loader {
        static_assert(sizeof(Tag) == 0, "Context loader not found for this tag.");
    };

    // --- The C++20 Verified Loader Interface Blueprint ---
    template <typename Tag>
    concept ValidStateTag = requires(const resultset_view& rows, diagnostics* diag) {
        typename context_state_loader<Tag>::state_type;
        { context_state_loader<Tag>::get_query() } -> std::same_as<std::string>;
        { context_state_loader<Tag>::parse(rows, diag) } -> std::same_as<std::unique_ptr<typename context_state_loader<Tag>::state_type>>;
    };

// --- The Query Aggregator Registry ---
struct query_collector {
    request combined_queries{};

    // Callbacks match the boost signature
    using HydrateCallback = std::function<std::unique_ptr<context_state>(const resultset_view&, diagnostics*)>;
    std::vector<HydrateCallback> parsers;

    inline void add_query(const std::string_view& sql, HydrateCallback parser_func) {
        combined_queries.add_query(sql, {}); // TODO: setting parameters?
        parsers.push_back(std::move(parser_func));
    }
};

// --- Runtime Storage Connection Context ---
class context {
private:
    static constexpr size_t MAX_CACHED_STATES = 16;
    std::unique_ptr<context_state> cache_slots[MAX_CACHED_STATES];
    size_t active_slots_count = 0;

    template <typename StateType>
    [[nodiscard]] StateType* find_cached_state() noexcept {
        for (size_t i = 0; i < active_slots_count; ++i) {
            if (cache_slots[i] && cache_slots[i]->get_type_id() == &typeid(StateType)) {
                return static_cast<StateType*>(cache_slots[i].get());
            }
        }
        return nullptr;
    }

public:
    void commit_state(std::unique_ptr<context_state> fresh_state) noexcept {
        if (!fresh_state) return;
        const void* target_id = fresh_state->get_type_id();

        for (size_t i = 0; i < active_slots_count; ++i) {
            if (cache_slots[i] && cache_slots[i]->get_type_id() == target_id) {
                cache_slots[i] = std::move(fresh_state);
                return;
            }
        }

        if (active_slots_count < MAX_CACHED_STATES) {
            cache_slots[active_slots_count] = std::move(fresh_state);
            ++active_slots_count;
        }
    }

    template <ValidStateTag Tag>
    [[nodiscard]] boost::system::error_code resolve(typename context_state_loader<Tag>::state_type& ctx) noexcept {
        using concrete_state = typename context_state_loader<Tag>::state_type;
        auto* ptr = find_cached_state<concrete_state>();
        if (!ptr) {
            return client_errc::context_not_found;
        }
        ctx = *ptr;
        return {};
    }
};

template <ValidStateTag Tag>
[[nodiscard]] auto make_context() noexcept
{
    return typename context_state_loader<Tag>::state_type{};
}

}  // namespace nativepg::context

#endif
