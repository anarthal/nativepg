//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_CONTEXT_DB_INFO_HPP
#define NATIVEPG_CONTEXT_DB_INFO_HPP

#include <string>

#include "context.hpp"
#include "nativepg/dynamic_resultset.hpp"

namespace nativepg::context {


struct db_info_tag {};
template <>
struct context_state_loader<db_info_tag> {
    struct state_type : public context_state {
        std::string database_name;
        std::string encoding;
        std::string collation;
        std::string ctype;
        std::string server_version;
        std::string search_path;

        [[nodiscard]] const void* get_type_id() const noexcept override { return &typeid(state_type); }
    };

    static std::string get_query() noexcept {
        return R"sql(
            SELECT
                current_database() AS database_name,
                pg_encoding_to_char(encoding) AS encoding,
                datcollate AS collation,
                datctype AS ctype,
                current_setting('server_version') AS server_version,
                current_setting('search_path') AS search_path
            FROM pg_catalog.pg_database
            WHERE datname = current_database();
            )sql";
    }

    static std::unique_ptr<state_type> parse(const resultset_view& result, diagnostics* diag) noexcept {
        state_type state;
        state.database_name = std::string(result.rows()[0][0].data_str());
        state.encoding = std::string(result.rows()[0][1].data_str());
        state.collation = std::string(result.rows()[0][2].data_str());
        state.ctype = std::string(result.rows()[0][3].data_str());
        state.server_version = std::string(result.rows()[0][4].data_str());
        state.search_path = std::string(result.rows()[0][5].data_str());
        return std::make_unique<state_type>(state);
    }
};

}

#endif
