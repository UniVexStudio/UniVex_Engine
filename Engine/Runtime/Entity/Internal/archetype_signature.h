// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstddef>
#include <typeindex>
#include <vector>

#include "hash.h"

namespace UVE::Scene::Detail {

/// The sorted set of component types making up one archetype. Private implementation detail —
/// never appears in any public header under engine/scene/include/. Two signatures with the same
/// types (regardless of insertion order) compare equal and hash equal, so they key
/// EntityManager's archetype-lookup map correctly.
class ArchetypeSignature final {
public:
    ArchetypeSignature() = default;
    explicit ArchetypeSignature(std::vector<std::type_index> componentTypes);

    [[nodiscard]] bool Contains(std::type_index type) const;
    [[nodiscard]] bool IsSupersetOf(const ArchetypeSignature& other) const;
    [[nodiscard]] ArchetypeSignature With(std::type_index type) const;
    [[nodiscard]] ArchetypeSignature Without(std::type_index type) const;
    [[nodiscard]] const std::vector<std::type_index>& GetTypes() const noexcept;

    [[nodiscard]] bool operator==(const ArchetypeSignature& other) const noexcept {
        return m_sortedTypes == other.m_sortedTypes;
    }

private:
    std::vector<std::type_index> m_sortedTypes;
};

} // namespace UVE::Scene::Detail

template <>
struct std::hash<UVE::Scene::Detail::ArchetypeSignature> {
    [[nodiscard]] std::size_t operator()(const UVE::Scene::Detail::ArchetypeSignature& signature) const noexcept {
        // The shared boost-formula combiner: byte-identical to the hand-rolled loop it replaced.
        std::size_t combined = 0;
        for (const std::type_index& type : signature.GetTypes()) {
            UVE::Utilities::HashCombine(combined, type.hash_code());
        }
        return combined;
    }
};
