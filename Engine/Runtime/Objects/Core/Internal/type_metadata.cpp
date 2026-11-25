// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "type_metadata.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace UVE::Core {
namespace {

[[nodiscard]] bool IsBoundedIdentifier(std::string_view value) noexcept {
    return !value.empty() && value.size() <= TypeMetadataRegistry::kMaximumIdentifierBytes;
}

[[nodiscard]] bool IsBoundedDisplayName(const std::string& value) noexcept {
    return !value.empty() && value.size() <= TypeMetadataRegistry::kMaximumDisplayNameBytes;
}

/// The four factory hooks are declared together by BindType or not at all. Half of them is
/// always a mistake - a create with no destroy leaks, a clone with no assign cannot be put back -
/// and it is worth rejecting rather than discovering at the first generic edit.
[[nodiscard]] bool HasPartialFactory(const TypeMetadataEntry& entry) noexcept {
    const bool anyDeclared = entry.createDefaultInstance != nullptr || entry.destroyInstance != nullptr ||
                             entry.cloneInstance != nullptr || entry.assignInstance != nullptr;
    return anyDeclared && !entry.HasFactory();
}

/// An optional identifier: absent is fine, present must be bounded like any other identifier.
[[nodiscard]] bool IsBoundedOptional(const std::string& value) noexcept {
    return value.empty() || value.size() <= TypeMetadataRegistry::kMaximumIdentifierBytes;
}

/// A declared enum must offer distinct values with bounded, non-empty labels, or the inspector
/// would render a dropdown that cannot round-trip a selection back to a value.
[[nodiscard]] bool HasMalformedEnumEntries(const TypeMetadataProperty& property) noexcept {
    if (property.enumEntries.size() > TypeMetadataRegistry::kMaximumMembersPerType) {
        return true;
    }
    std::vector<std::int64_t> values;
    values.reserve(property.enumEntries.size());
    for (const TypeMetadataEnumEntry& option : property.enumEntries) {
        if (!IsBoundedDisplayName(option.label)) {
            return true;
        }
        if (std::find(values.begin(), values.end(), option.value) != values.end()) {
            return true;
        }
        values.push_back(option.value);
    }
    return false;
}

[[nodiscard]] bool ExceedsMemberCapacity(const TypeMetadataEntry& entry) noexcept {
    return entry.properties.size() > TypeMetadataRegistry::kMaximumMembersPerType ||
           entry.methods.size() > TypeMetadataRegistry::kMaximumMembersPerType -
                                      std::min(entry.properties.size(),
                                               TypeMetadataRegistry::kMaximumMembersPerType);
}

[[nodiscard]] bool HasDuplicateMemberNames(const TypeMetadataEntry& entry) {
    std::vector<std::string_view> names;
    names.reserve(std::min(entry.properties.size(), TypeMetadataRegistry::kMaximumMembersPerType));
    for (const TypeMetadataProperty& property : entry.properties) {
        if (!IsBoundedIdentifier(property.name) || !IsBoundedDisplayName(property.displayName) ||
            !IsBoundedIdentifier(property.typeId.ToString()) || !IsBoundedOptional(property.section) ||
            !IsBoundedOptional(property.customDrawerId) || !IsBoundedOptional(property.resolvedByProperty) ||
            property.tooltip.size() > TypeMetadataRegistry::kMaximumDisplayNameBytes ||
            HasMalformedEnumEntries(property)) {
            return true;
        }
        if (std::find(names.begin(), names.end(), property.name) != names.end()) {
            return true;
        }
        names.push_back(property.name);
    }
    for (const TypeMetadataMethod& method : entry.methods) {
        if (!IsBoundedIdentifier(method.name) || !IsBoundedDisplayName(method.displayName)) {
            return true;
        }
        if (std::find(names.begin(), names.end(), method.name) != names.end()) {
            return true;
        }
        names.push_back(method.name);
    }
    return false;
}

/// A resolved-by link must point at a different property of the same entry. A dangling name would
/// leave an inspector with nothing to show; a self-reference would show the choice as its own answer.
[[nodiscard]] bool HasDanglingResolvedBy(const TypeMetadataEntry& entry) noexcept {
    return std::any_of(entry.properties.cbegin(), entry.properties.cend(), [&entry](const auto& property) {
        if (property.resolvedByProperty.empty()) {
            return false;
        }
        return property.resolvedByProperty == property.name ||
               std::none_of(entry.properties.cbegin(), entry.properties.cend(), [&property](const auto& other) {
                   return other.name == property.resolvedByProperty;
               });
    });
}

// Each host a bounded identifier, none of them the type itself, and none listed twice.
[[nodiscard]] bool AreNestingHostsValid(const TypeMetadataEntry& entry) {
    for (std::size_t index = 0U; index < entry.nestedUnderTypeIds.size(); ++index) {
        const Strings::StringId& host = entry.nestedUnderTypeIds[index];
        if (!IsBoundedIdentifier(host.ToString()) || host == entry.typeId ||
            std::find(entry.nestedUnderTypeIds.begin(), entry.nestedUnderTypeIds.begin() +
                                                            static_cast<std::ptrdiff_t>(index),
                      host) != entry.nestedUnderTypeIds.begin() + static_cast<std::ptrdiff_t>(index)) {
            return false;
        }
    }
    return true;
}

} // namespace

TypeMetadataRegistrationResult TypeMetadataRegistry::RegisterType(TypeMetadataEntry entry) {
    if (!IsBoundedIdentifier(entry.typeId.ToString()) || !IsBoundedDisplayName(entry.displayName) || entry.version == 0U ||
        HasPartialFactory(entry) ||
        ExceedsMemberCapacity(entry) ||
        HasDuplicateMemberNames(entry) ||
        HasDanglingResolvedBy(entry) ||
        !AreNestingHostsValid(entry)) {
        return {TypeMetadataRegistrationCode::InvalidEntry,
                "Type metadata requires bounded identity, display, version, unique members and resolvable links."};
    }
    if (FindType(entry.typeId) != nullptr) {
        return {TypeMetadataRegistrationCode::DuplicateType,
                "Type metadata registration rejected a duplicate type identifier."};
    }
    // Two entries claiming the same C++ type would make the std::type_index bridge ambiguous:
    // an entity's live component could resolve to either set of metadata.
    if (FindTypeByIndex(entry.typeIndex) != nullptr) {
        return {TypeMetadataRegistrationCode::DuplicateType,
                "Type metadata registration rejected a duplicate native type."};
    }
    if (m_entries.size() >= kMaximumTypes) {
        return {TypeMetadataRegistrationCode::CapacityExceeded,
                "Type metadata registry capacity has been reached."};
    }
    m_entries.push_back(std::move(entry));
    if (m_generation < std::numeric_limits<std::uint64_t>::max()) {
        ++m_generation;
    }
    return {TypeMetadataRegistrationCode::Registered, "Type metadata was registered."};
}

const TypeMetadataEntry* TypeMetadataRegistry::FindTypeByIndex(
    const std::type_index typeIndex) const noexcept {
    if (typeIndex == std::type_index(typeid(void))) {
        return nullptr;
    }
    const auto iterator = std::find_if(m_entries.cbegin(), m_entries.cend(), [typeIndex](const auto& entry) {
        return entry.typeIndex == typeIndex;
    });
    return iterator == m_entries.cend() ? nullptr : &*iterator;
}

const TypeMetadataEntry* TypeMetadataRegistry::FindType(const Strings::StringId typeId) const noexcept {
    const auto iterator = std::find_if(m_entries.cbegin(), m_entries.cend(), [typeId](const auto& entry) {
        return entry.typeId == typeId;
    });
    return iterator == m_entries.cend() ? nullptr : &*iterator;
}

TypeMetadataSnapshot TypeMetadataRegistry::GetSnapshot() const {
    TypeMetadataSnapshot snapshot{m_generation, false, m_entries};
    std::sort(snapshot.entries.begin(), snapshot.entries.end(), [](const auto& left, const auto& right) {
        if (left.kind != right.kind) {
            return static_cast<std::uint8_t>(left.kind) < static_cast<std::uint8_t>(right.kind);
        }
        return left.typeId.ToString() < right.typeId.ToString();
    });
    return snapshot;
}

std::size_t TypeMetadataRegistry::GetTypeCount() const noexcept {
    return m_entries.size();
}

std::uint64_t TypeMetadataRegistry::GetGeneration() const noexcept {
    return m_generation;
}

} // namespace UVE::Core
