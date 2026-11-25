#include "data_table_registry.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace UVE::Asset {

bool DataTableRegistry::Register(DataTable table) {
    const DataTableSnapshot candidate = table.GetSnapshot();
    if (!IsValidTableSnapshot(candidate) || Contains(candidate.name) ||
        m_tables.size() >= kMaximumTables) {
        return false;
    }
    m_tables.push_back(std::move(table));
    IncrementGeneration();
    return true;
}

bool DataTableRegistry::Remove(const std::string_view name) {
    const auto iterator = std::find_if(m_tables.begin(), m_tables.end(), [name](const DataTable& table) {
        return table.GetSnapshot().name == name;
    });
    if (iterator == m_tables.end()) {
        return false;
    }
    m_tables.erase(iterator);
    IncrementGeneration();
    return true;
}

bool DataTableRegistry::Clear() noexcept {
    if (m_tables.empty()) {
        return false;
    }
    m_tables.clear();
    IncrementGeneration();
    return true;
}

bool DataTableRegistry::TryGetSnapshot(const std::string_view name, DataTableSnapshot& snapshot) const {
    const auto iterator = std::find_if(m_tables.begin(), m_tables.end(), [name](const DataTable& table) {
        return table.GetSnapshot().name == name;
    });
    if (iterator == m_tables.end()) {
        return false;
    }
    snapshot = iterator->GetSnapshot();
    return true;
}

DataTableCatalogSnapshot DataTableRegistry::GetCatalogSnapshot() const {
    DataTableCatalog catalog;
    for (const DataTable& table : m_tables) {
        const DataTableSnapshot snapshot = table.GetSnapshot();
        static_cast<void>(catalog.Upsert(snapshot));
    }
    DataTableCatalogSnapshot snapshot = catalog.GetSnapshot();
    snapshot.generation = m_generation;
    return snapshot;
}

bool DataTableRegistry::Contains(const std::string_view name) const {
    return std::any_of(m_tables.begin(), m_tables.end(), [name](const DataTable& table) {
        return table.GetSnapshot().name == name;
    });
}

std::size_t DataTableRegistry::Size() const noexcept {
    return m_tables.size();
}

void DataTableRegistry::IncrementGeneration() noexcept {
    if (m_generation < std::numeric_limits<std::uint64_t>::max()) {
        ++m_generation;
    }
}

bool DataTableRegistry::IsValidTableSnapshot(const DataTableSnapshot& snapshot) noexcept {
    return !snapshot.name.empty() && snapshot.diagnostics.empty();
}

} // namespace UVE::Asset
