// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/object/type_metadata_uve.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace UVE::Core {
namespace {

[[nodiscard]] bool IsBoundedIdentifierUVE(const std::string& value) noexcept {
    return !value.empty() && value.size() <= TypeMetadataRegistryUVE::kMaximumIdentifierBytesUVE;
}

[[nodiscard]] bool IsBoundedDisplayNameUVE(const std::string& value) noexcept {
    return !value.empty() && value.size() <= TypeMetadataRegistryUVE::kMaximumDisplayNameBytesUVE;
}

/// An optional identifier: absent is fine, present must be bounded like any other identifier.
[[nodiscard]] bool IsBoundedOptionalUVE(const std::string& value) noexcept {
    return value.empty() || value.size() <= TypeMetadataRegistryUVE::kMaximumIdentifierBytesUVE;
}

/// A declared enum must offer distinct values with bounded, non-empty labels, or the inspector
/// would render a dropdown that cannot round-trip a selection back to a value.
[[nodiscard]] bool HasMalformedEnumEntriesUVE(const TypeMetadataPropertyUVE& property) noexcept {
    if (property.enumEntries.size() > TypeMetadataRegistryUVE::kMaximumMembersPerTypeUVE) {
        return true;
    }
    std::vector<std::int64_t> values;
    values.reserve(property.enumEntries.size());
    for (const TypeMetadataEnumEntryUVE& option : property.enumEntries) {
        if (!IsBoundedDisplayNameUVE(option.label)) {
            return true;
        }
        if (std::find(values.begin(), values.end(), option.value) != values.end()) {
            return true;
        }
        values.push_back(option.value);
    }
    return false;
}

[[nodiscard]] bool ExceedsMemberCapacityUVE(const TypeMetadataEntryUVE& entry) noexcept {
    return entry.properties.size() > TypeMetadataRegistryUVE::kMaximumMembersPerTypeUVE ||
           entry.methods.size() > TypeMetadataRegistryUVE::kMaximumMembersPerTypeUVE -
                                      std::min(entry.properties.size(),
                                               TypeMetadataRegistryUVE::kMaximumMembersPerTypeUVE);
}

[[nodiscard]] bool HasDuplicateMemberNamesUVE(const TypeMetadataEntryUVE& entry) noexcept {
    std::vector<std::string_view> names;
    names.reserve(std::min(entry.properties.size(), TypeMetadataRegistryUVE::kMaximumMembersPerTypeUVE));
    for (const TypeMetadataPropertyUVE& property : entry.properties) {
        if (!IsBoundedIdentifierUVE(property.name) || !IsBoundedDisplayNameUVE(property.displayName) ||
            !IsBoundedIdentifierUVE(property.typeId) || !IsBoundedOptionalUVE(property.section) ||
            !IsBoundedOptionalUVE(property.customDrawerId) ||
            property.tooltip.size() > TypeMetadataRegistryUVE::kMaximumDisplayNameBytesUVE ||
            HasMalformedEnumEntriesUVE(property)) {
            return true;
        }
        if (std::find(names.begin(), names.end(), property.name) != names.end()) {
            return true;
        }
        names.push_back(property.name);
    }
    for (const TypeMetadataMethodUVE& method : entry.methods) {
        if (!IsBoundedIdentifierUVE(method.name) || !IsBoundedDisplayNameUVE(method.displayName)) {
            return true;
        }
        if (std::find(names.begin(), names.end(), method.name) != names.end()) {
            return true;
        }
        names.push_back(method.name);
    }
    return false;
}

} // namespace

TypeMetadataRegistrationResultUVE TypeMetadataRegistryUVE::RegisterTypeUVE(TypeMetadataEntryUVE entry) {
    if (!IsBoundedIdentifierUVE(entry.typeId) || !IsBoundedDisplayNameUVE(entry.displayName) || entry.version == 0U ||
        ((entry.createDefaultInstance == nullptr) != (entry.destroyInstance == nullptr)) ||
        ExceedsMemberCapacityUVE(entry) ||
        HasDuplicateMemberNamesUVE(entry)) {
        return {TypeMetadataRegistrationCodeUVE::InvalidEntry,
                "Type metadata requires bounded identity, display, version, and unique members."};
    }
    if (FindTypeUVE(entry.typeId) != nullptr) {
        return {TypeMetadataRegistrationCodeUVE::DuplicateType,
                "Type metadata registration rejected a duplicate type identifier."};
    }
    // Two entries claiming the same C++ type would make the std::type_index bridge ambiguous:
    // an entity's live component could resolve to either set of metadata.
    if (FindTypeByIndexUVE(entry.typeIndex) != nullptr) {
        return {TypeMetadataRegistrationCodeUVE::DuplicateType,
                "Type metadata registration rejected a duplicate native type."};
    }
    if (m_entries.size() >= kMaximumTypesUVE) {
        return {TypeMetadataRegistrationCodeUVE::CapacityExceeded,
                "Type metadata registry capacity has been reached."};
    }
    m_entries.push_back(std::move(entry));
    if (m_generation < std::numeric_limits<std::uint64_t>::max()) {
        ++m_generation;
    }
    return {TypeMetadataRegistrationCodeUVE::Registered, "Type metadata was registered."};
}

const TypeMetadataEntryUVE* TypeMetadataRegistryUVE::FindTypeByIndexUVE(
    const std::type_index typeIndex) const noexcept {
    if (typeIndex == std::type_index(typeid(void))) {
        return nullptr;
    }
    const auto iterator = std::find_if(m_entries.cbegin(), m_entries.cend(), [typeIndex](const auto& entry) {
        return entry.typeIndex == typeIndex;
    });
    return iterator == m_entries.cend() ? nullptr : &*iterator;
}

const TypeMetadataEntryUVE* TypeMetadataRegistryUVE::FindTypeUVE(const std::string_view typeId) const noexcept {
    const auto iterator = std::find_if(m_entries.cbegin(), m_entries.cend(), [typeId](const auto& entry) {
        return entry.typeId == typeId;
    });
    return iterator == m_entries.cend() ? nullptr : &*iterator;
}

TypeMetadataSnapshotUVE TypeMetadataRegistryUVE::GetSnapshotUVE() const {
    TypeMetadataSnapshotUVE snapshot{m_generation, false, m_entries};
    std::sort(snapshot.entries.begin(), snapshot.entries.end(), [](const auto& left, const auto& right) {
        if (left.kind != right.kind) {
            return static_cast<std::uint8_t>(left.kind) < static_cast<std::uint8_t>(right.kind);
        }
        return left.typeId < right.typeId;
    });
    return snapshot;
}

std::size_t TypeMetadataRegistryUVE::GetTypeCountUVE() const noexcept {
    return m_entries.size();
}

std::uint64_t TypeMetadataRegistryUVE::GetGenerationUVE() const noexcept {
    return m_generation;
}

} // namespace UVE::Core
