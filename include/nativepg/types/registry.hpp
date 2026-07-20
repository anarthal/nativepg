//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_TYPES_REGISTRY_HPP
#define NATIVEPG_TYPES_REGISTRY_HPP

#include <boost/describe/class.hpp>

#include <cstdint>
#include <memory>
#include <ranges>
#include <string>
#include <vector>

namespace nativepg::detail::loaders {
    class type_registry_loader;
}

namespace nativepg::types {

struct type_info
{
    std::uint32_t type_oid;
    std::string schema_name;
    std::string type_name;
    std::string extension_name;
    short schema_path_seqno;
    std::uint32_t array_element_oid;
    char type;
    short storage_bytes;
};
BOOST_DESCRIBE_STRUCT(
    type_info,
    (),
    (type_oid,
     schema_name,
     type_name,
     extension_name,
     schema_path_seqno,
     array_element_oid,
     type,
     storage_bytes)
);

class type_registry
{
    std::vector<type_info> table_;

public:
    type_registry(const std::vector<type_info>& table) noexcept : table_(table) {}
    type_registry(const type_registry&) noexcept = default;
    type_registry(type_registry&& other) noexcept { table_ = std::move(other.table_); }
    type_registry& operator=(const type_registry& other) noexcept
    {
        table_ = other.table_;
        return *this;
    }
    type_registry& operator=(type_registry&& other) noexcept
    {
        table_ = std::move(other.table_);
        return *this;
    }
    ~type_registry() = default;

    // Range support, so instances can be iterated
    [[nodiscard]] auto begin() const { return table_.begin(); }
    [[nodiscard]] auto end() const { return table_.end(); }
    [[nodiscard]] std::size_t size() const { return table_.size(); }
    [[nodiscard]] bool empty() const { return table_.empty(); }

    [[nodiscard]] auto find_by_oid(std::uint32_t oid) const
    {
        return std::ranges::find_if(table_, [oid](const type_info& info) { return info.type_oid == oid; });
    }

    [[nodiscard]] auto find_by_type_name(const std::string& schema_name, const std::string& type_name) const
    {
        return std::ranges::find_if(table_, [&schema_name, &type_name](const type_info& info) {
            return info.schema_name == schema_name && info.type_name == type_name;
        });
    }

    [[nodiscard]] auto find_by_type_name(const std::string& type_name) const
    {
        return std::ranges::find_if(table_, [&type_name](const type_info& info) {
            return info.type_name == type_name;
        });
    }
};

}  // namespace nativepg

#endif  // NATIVEPG_TYPES_REGISTRY_HPP
