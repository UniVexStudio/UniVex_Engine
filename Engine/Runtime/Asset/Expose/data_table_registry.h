#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "data_table.h"

namespace UVE::Asset {

class DataTableRegistry final {
public:
    static constexpr std::size_t kMaximumTables = 128U;

    DataTableRegistry() = default;
    DataTableRegistry(const DataTableRegistry&) = delete;
    DataTableRegistry& operator=(const DataTableRegistry&) = delete;
    DataTableRegistry(DataTableRegistry&&) noexcept = default;
    DataTableRegistry& operator=(DataTableRegistry&&) noexcept = default;
    ~DataTableRegistry() = default;

    [[nodiscard]] bool Register(DataTable table);
    [[nodiscard]] bool Remove(std::string_view name);
    [[nodiscard]] bool Clear() noexcept;
    [[nodiscard]] bool TryGetSnapshot(std::string_view name, DataTableSnapshot& snapshot) const;
    [[nodiscard]] DataTableCatalogSnapshot GetCatalogSnapshot() const;
    [[nodiscard]] bool Contains(std::string_view name) const;
    [[nodiscard]] std::size_t Size() const noexcept;

private:
    void IncrementGeneration() noexcept;
    [[nodiscard]] static bool IsValidTableSnapshot(const DataTableSnapshot& snapshot) noexcept;

    std::vector<DataTable> m_tables;
    std::uint64_t m_generation = 1U;
};

} // namespace UVE::Asset
