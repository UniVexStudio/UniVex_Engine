// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "archetype_signature.h"

#include <algorithm>
#include <utility>

namespace UVE::Scene::Detail {

ArchetypeSignature::ArchetypeSignature(std::vector<std::type_index> componentTypes)
    : m_sortedTypes(std::move(componentTypes)) {
    std::sort(m_sortedTypes.begin(), m_sortedTypes.end());
}

bool ArchetypeSignature::Contains(std::type_index type) const {
    return std::find(m_sortedTypes.begin(), m_sortedTypes.end(), type) != m_sortedTypes.end();
}

bool ArchetypeSignature::IsSupersetOf(const ArchetypeSignature& other) const {
    for (const std::type_index& type : other.m_sortedTypes) {
        if (!Contains(type)) {
            return false;
        }
    }
    return true;
}

ArchetypeSignature ArchetypeSignature::With(std::type_index type) const {
    std::vector<std::type_index> types = m_sortedTypes;
    types.push_back(type);
    return ArchetypeSignature(std::move(types));
}

ArchetypeSignature ArchetypeSignature::Without(std::type_index type) const {
    std::vector<std::type_index> types;
    types.reserve(m_sortedTypes.size());
    for (const std::type_index& existingType : m_sortedTypes) {
        if (existingType != type) {
            types.push_back(existingType);
        }
    }
    return ArchetypeSignature(std::move(types));
}

const std::vector<std::type_index>& ArchetypeSignature::GetTypes() const noexcept {
    return m_sortedTypes;
}

} // namespace UVE::Scene::Detail
